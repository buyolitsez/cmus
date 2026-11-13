/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "convert.h"
#include "input.h"
#include "ip.h"
#include "pcm.h"
#include "xmalloc.h"
#include "file.h"
#include "path.h"
#include "utils.h"
#include "cmus.h"
#include "options.h"
#include "list.h"
#include "mergesort.h"
#include "misc.h"
#include "debug.h"
#include "ui_curses.h"
#include "locking.h"
#include "xstrjoin.h"

#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <strings.h>

struct input_plugin {
	const struct input_plugin_ops *ops;
	struct input_plugin_data data;
	unsigned int open : 1;
	unsigned int eof : 1;

	/* cached duration, -1 = unset */
	int duration;
	/* cached bitrate, -1 = unset */
	long bitrate;
	/* cached codec, NULL = unset */
	char *codec;
	/* cached codec_profile, NULL = unset */
	char *codec_profile;

	/*
	 * pcm is converted to 16-bit signed little-endian stereo
	 * NOTE: no conversion is done if channels > 2 or bits > 16
	 */
	void (*pcm_convert)(void *, const void *, int);
	void (*pcm_convert_in_place)(void *, int);
	/*
	 * 4  if 8-bit mono
	 * 2  if 8-bit stereo or 16-bit mono
	 * 1  otherwise
	 */
	int pcm_convert_scale;
};

struct ip {
	struct list_head node;
	char *name;
	void *handle;

	int priority;
	const char * const *extensions;
	const char * const *mime_types;
	const struct input_plugin_ops *ops;
	const struct input_plugin_opt *options;
};

static const char *plugin_dir;
static LIST_HEAD(ip_head);

/* protects ip->priority and ip_head */
static pthread_rwlock_t ip_lock = CMUS_RWLOCK_INITIALIZER;

#define ip_rdlock() cmus_rwlock_rdlock(&ip_lock)
#define ip_wrlock() cmus_rwlock_wrlock(&ip_lock)
#define ip_unlock() cmus_rwlock_unlock(&ip_lock)

static const struct input_plugin_ops *
get_ops_by_extension_locked(const char *ext, struct list_head **headp)
{
	struct list_head *node = *headp;

	for (node = node->next; node != &ip_head; node = node->next) {
		struct ip *ip = list_entry(node, struct ip, node);
		const char * const *exts = ip->extensions;
		int i;

		if (ip->priority <= 0) {
			break;
		}

		for (i = 0; exts[i]; i++) {
			if (strcasecmp(ext, exts[i]) == 0 || strcmp("*", exts[i]) == 0) {
				*headp = node;
				return ip->ops;
			}
		}
	}
	return NULL;
}

static const struct input_plugin_ops *
get_ops_by_extension(const char *ext, struct list_head **headp)
{
	ip_rdlock();
	const struct input_plugin_ops *rv = get_ops_by_extension_locked(ext,
			headp);
	ip_unlock();
	return rv;
}

static const struct input_plugin_ops *
get_ops_by_mime_type_locked(const char *mime_type)
{
	struct ip *ip;

	list_for_each_entry(ip, &ip_head, node) {
		const char * const *types = ip->mime_types;
		int i;

		if (ip->priority <= 0) {
			break;
		}

		for (i = 0; types[i]; i++) {
			if (strcasecmp(mime_type, types[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static const struct input_plugin_ops *
get_ops_by_mime_type(const char *mime_type)
{
	ip_rdlock();
	const struct input_plugin_ops *rv =
		get_ops_by_mime_type_locked(mime_type);
	ip_unlock();
	return rv;
}

static void ip_init(struct input_plugin *ip, char *filename)
{
	const struct input_plugin t = {
		.pcm_convert_scale  = -1,
		.duration           = -1,
		.bitrate            = -1,
		.data = {
			.fd         = -1,
			.filename   = filename,
			.channel_map = CHANNEL_MAP_INIT
		}
	};
	*ip = t;
}

static void ip_reset(struct input_plugin *ip, int close_fd)
{
	int fd = ip->data.fd;
	ip_init(ip, ip->data.filename);
	if (fd != -1) {
		if (close_fd)
			close(fd);
		else {
			lseek(fd, 0, SEEK_SET);
			ip->data.fd = fd;
		}
	}
}

static int open_file_locked(struct input_plugin *ip)
{
	const struct input_plugin_ops *ops;
	struct list_head *head = &ip_head;
	const char *ext;
	int rc = 0;

	ext = get_extension(ip->data.filename);
	if (!ext)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;

	ops = get_ops_by_extension(ext, &head);
	if (!ops)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;

	ip->data.fd = open(ip->data.filename, O_RDONLY);
	if (ip->data.fd == -1)
		return -IP_ERROR_ERRNO;

	while (1) {
		ip->ops = ops;
		rc = ip->ops->open(&ip->data);
		if (rc != -IP_ERROR_UNSUPPORTED_FILE_TYPE)
			break;

		ops = get_ops_by_extension(ext, &head);
		if (!ops)
			break;

		ip_reset(ip, 0);
		d_print("fallback: try next plugin for `%s'\n", ip->data.filename);
	}

	return rc;
}

static int open_file(struct input_plugin *ip)
{
	ip_rdlock();
	int rv = open_file_locked(ip);
	ip_unlock();
	return rv;
}

static int sort_ip(const struct list_head *a_, const struct list_head *b_)
{
	const struct ip *a = list_entry(a_, struct ip, node);
	const struct ip *b = list_entry(b_, struct ip, node);
	return b->priority - a->priority;
}

void ip_load_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	plugin_dir = xstrjoin(cmus_lib_dir, "/ip");
	dir = opendir(plugin_dir);
	if (dir == NULL) {
		error_msg("couldn't open directory `%s': %s", plugin_dir, strerror(errno));
		return;
	}

	ip_wrlock();
	while ((d = (struct dirent *) readdir(dir)) != NULL) {
		char filename[512];
		struct ip *ip;
		void *so;
		char *ext;
		const int *priority_ptr;
		const unsigned *abi_version_ptr;
		bool err = false;

		if (d->d_name[0] == '.')
			continue;
		ext = strrchr(d->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".so"))
			continue;

		snprintf(filename, sizeof(filename), "%s/%s", plugin_dir, d->d_name);

		so = dlopen(filename, RTLD_NOW);
		if (so == NULL) {
			d_print("%s: %s\n", filename, dlerror());
			continue;
		}

		ip = xnew(struct ip, 1);

		abi_version_ptr = dlsym(so, "ip_abi_version");
		priority_ptr = dlsym(so, "ip_priority");
		ip->extensions = dlsym(so, "ip_extensions");
		ip->mime_types = dlsym(so, "ip_mime_types");
		ip->ops = dlsym(so, "ip_ops");
		ip->options = dlsym(so, "ip_options");
		if (!priority_ptr || !ip->extensions || !ip->mime_types || !ip->ops || !ip->options) {
			error_msg("%s: missing symbol", filename);
			err = true;
		}
		if (!abi_version_ptr || *abi_version_ptr != IP_ABI_VERSION) {
			error_msg("%s: incompatible plugin version", filename);
			err = true;
		}
		if (err) {
			free(ip);
			dlclose(so);
			continue;
		}
		ip->priority = *priority_ptr;

		ip->name = xstrndup(d->d_name, ext - d->d_name);
		ip->handle = so;

		list_add_tail(&ip->node, &ip_head);
	}
	list_mergesort(&ip_head, sort_ip);
	closedir(dir);
	ip_unlock();
}

struct input_plugin *ip_new(const char *filename)
{
	struct input_plugin *ip = xnew(struct input_plugin, 1);

	ip_init(ip, xstrdup(filename));
	return ip;
}

void ip_delete(struct input_plugin *ip)
{
	if (ip->open)
		ip_close(ip);
	free(ip->data.filename);
	free(ip);
}

int ip_open(struct input_plugin *ip)
{
	int rc;

	BUG_ON(ip->open);

	/* set fd and ops, call ops->open */
	if (is_cue_url(ip->data.filename)) {
		ip->ops = get_ops_by_mime_type("application/x-cue");
		rc = ip->ops ? ip->ops->open(&ip->data) : 1;
	} else {
		rc = open_file(ip);
	}

	if (rc) {
		d_print("opening `%s' failed: %d %s\n", ip->data.filename, rc,
				rc == -1 ? strerror(errno) : "");
		ip_reset(ip, 1);
		return rc;
	}

	unsigned bits = sf_get_bits(ip->data.sf);
	unsigned channels = sf_get_channels(ip->data.sf);
	unsigned rate = sf_get_rate(ip->data.sf);
	if (!bits || !channels || !rate) {
		d_print("corrupt file: bits = %u, channels = %u, rate = %u\n",
				bits, channels, rate);
		return -IP_ERROR_FILE_FORMAT;
	}

	ip->open = 1;
	return 0;
}

void ip_setup(struct input_plugin *ip)
{
	unsigned int bits, is_signed, channels;
	sample_format_t sf = ip->data.sf;

	bits = sf_get_bits(sf);
	is_signed = sf_get_signed(sf);
	channels = sf_get_channels(sf);

	ip->pcm_convert_scale = 1;
	ip->pcm_convert = NULL;
	ip->pcm_convert_in_place = NULL;

	if (bits <= 16 && channels <= 2) {
		unsigned int mask = ((bits >> 2) & 4) | (is_signed << 1);

		ip->pcm_convert = pcm_conv[mask | (channels - 1)];
		ip->pcm_convert_in_place = pcm_conv_in_place[mask | sf_get_bigendian(sf)];

		ip->pcm_convert_scale = (3 - channels) * (3 - bits / 8);
	}

	d_print("pcm convert: scale=%d convert=%d convert_in_place=%d\n",
			ip->pcm_convert_scale,
			ip->pcm_convert != NULL,
			ip->pcm_convert_in_place != NULL);
}

int ip_close(struct input_plugin *ip)
{
	int rc;

	rc = ip->ops->close(&ip->data);
	BUG_ON(ip->data.private);
	if (ip->data.fd != -1)
		close(ip->data.fd);
	ip_init(ip, ip->data.filename);
	return rc;
}

int ip_read(struct input_plugin *ip, char *buffer, int count)
{
	struct timeval tv;
	fd_set readfds;
	/* 4608 seems to be optimal for mp3s, 4096 for oggs */
	char tmp[8 * 1024];
	char *buf;
	int sample_size;
	int rc;

	BUG_ON(count <= 0);

	FD_ZERO(&readfds);
	FD_SET(ip->data.fd, &readfds);
	/* zero timeout -> return immediately */
	tv.tv_sec = 0;
	tv.tv_usec = 50e3;
	rc = select(ip->data.fd + 1, &readfds, NULL, NULL, &tv);
	if (rc == -1) {
		if (errno == EINTR)
			errno = EAGAIN;
		return -1;
	}
	if (rc == 0) {
		errno = EAGAIN;
		return -1;
	}

	buf = buffer;
	if (ip->pcm_convert_scale > 1) {
		/* use tmp buffer for 16-bit mono and 8-bit */
		buf = tmp;
		count /= ip->pcm_convert_scale;
		if (count > sizeof(tmp))
			count = sizeof(tmp);
	}

	rc = ip->ops->read(&ip->data, buf, count);
	if (rc == -1 && (errno == EAGAIN || errno == EINTR)) {
		errno = EAGAIN;
		return -1;
	}
	if (rc <= 0) {
		ip->eof = 1;
		return rc;
	}

	BUG_ON(rc % sf_get_frame_size(ip->data.sf) != 0);

	sample_size = sf_get_sample_size(ip->data.sf);
	if (ip->pcm_convert_in_place != NULL)
		ip->pcm_convert_in_place(buf, rc / sample_size);
	if (ip->pcm_convert != NULL)
		ip->pcm_convert(buffer, tmp, rc / sample_size);
	return rc * ip->pcm_convert_scale;
}

int ip_seek(struct input_plugin *ip, double offset)
{
	int rc;

	rc = ip->ops->seek(&ip->data, offset);
	if (rc == 0)
		ip->eof = 0;
	return rc;
}

int ip_read_comments(struct input_plugin *ip, struct keyval **comments)
{
	struct keyval *kv = NULL;
	int rc;

	rc = ip->ops->read_comments(&ip->data, &kv);
	*comments = kv;
	return rc;
}

int ip_duration(struct input_plugin *ip)
{
	if (ip->duration == -1)
		ip->duration = ip->ops->duration(&ip->data);
	if (ip->duration < 0)
		return -1;
	return ip->duration;
}

int ip_bitrate(struct input_plugin *ip)
{
	if (ip->bitrate == -1)
		ip->bitrate = ip->ops->bitrate(&ip->data);
	if (ip->bitrate < 0)
		return -1;
	return ip->bitrate;
}

int ip_current_bitrate(struct input_plugin *ip)
{
	return ip->ops->bitrate_current(&ip->data);
}

char *ip_codec(struct input_plugin *ip)
{
	if (!ip->codec)
		ip->codec = ip->ops->codec(&ip->data);
	return ip->codec;
}

char *ip_codec_profile(struct input_plugin *ip)
{
	if (!ip->codec_profile)
		ip->codec_profile = ip->ops->codec_profile(&ip->data);
	return ip->codec_profile;
}

sample_format_t ip_get_sf(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.sf;
}

void ip_get_channel_map(struct input_plugin *ip, channel_position_t *channel_map)
{
	BUG_ON(!ip->open);
	channel_map_copy(channel_map, ip->data.channel_map);
}

const char *ip_get_filename(struct input_plugin *ip)
{
	return ip->data.filename;
}

int ip_eof(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->eof;
}

static void option_error(int rc)
{
	char *msg = ip_get_error_msg(NULL, rc, "setting option");
	error_msg("%s", msg);
	free(msg);
}

static void set_ip_option(void *data, const char *val)
{
	const struct input_plugin_opt *ipo = data;
	int rc;

	rc = ipo->set(val);
	if (rc)
		option_error(rc);
}

static void get_ip_option(void *data, char *buf, size_t size)
{
	const struct input_plugin_opt *ipo = data;
	char *val = NULL;

	ipo->get(&val);
	if (val) {
		strscpy(buf, val, size);
		free(val);
	}
}

static void set_ip_priority(void *data, const char *val)
{
	/* warn only once during the lifetime of the program. */
	static bool warned = false;
	long tmp;
	struct ip *ip = data;

	if (str_to_int(val, &tmp) == -1 || tmp < 0 || (long)(int)tmp != tmp) {
		error_msg("non-negative integer expected");
		return;
	}
	if (ui_initialized) {
		if (!warned) {
			static const char *msg =
				"Metadata might become inconsistent "
				"after this change. Continue? [y/N]";
			if (yes_no_query("%s", msg) != UI_QUERY_ANSWER_YES) {
				info_msg("Aborted");
				return;
			}
			warned = true;
		}
		info_msg("Run \":update-cache -f\" to refresh the metadata.");
	}

	ip_wrlock();
	ip->priority = (int)tmp;
	list_mergesort(&ip_head, sort_ip);
	ip_unlock();
}

static void get_ip_priority(void *data, char *val, size_t size)
{
	const struct ip *ip = data;
	ip_rdlock();
	snprintf(val, size, "%d", ip->priority);
	ip_unlock();
}

void ip_add_options(void)
{
	struct ip *ip;
	const struct input_plugin_opt *ipo;
	char key[64];

	ip_rdlock();
	list_for_each_entry(ip, &ip_head, node) {
		for (ipo = ip->options; ipo->name; ipo++) {
			snprintf(key, sizeof(key), "input.%s.%s", ip->name,
					ipo->name);
			option_add(xstrdup(key), ipo, get_ip_option,
					set_ip_option, NULL, 0);
		}
		snprintf(key, sizeof(key), "input.%s.priority", ip->name);
		option_add(xstrdup(key), ip, get_ip_priority, set_ip_priority, NULL, 0);
	}
	ip_unlock();
}

char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg)
{
	char buffer[1024];

	switch (-rc) {
	case IP_ERROR_ERRNO:
		snprintf(buffer, sizeof(buffer), "%s: %s", arg, strerror(errno));
		break;
	case IP_ERROR_UNRECOGNIZED_FILE_TYPE:
		snprintf(buffer, sizeof(buffer),
				"%s: unrecognized filename extension", arg);
		break;
	case IP_ERROR_UNSUPPORTED_FILE_TYPE:
		snprintf(buffer, sizeof(buffer),
				"%s: unsupported file format", arg);
		break;
	case IP_ERROR_FUNCTION_NOT_SUPPORTED:
		snprintf(buffer, sizeof(buffer),
				"%s: function not supported", arg);
		break;
	case IP_ERROR_FILE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: file format not supported or corrupted file",
				arg);
		break;
	case IP_ERROR_INVALID_URI:
		snprintf(buffer, sizeof(buffer), "%s: invalid URI", arg);
		break;
	case IP_ERROR_SAMPLE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: input plugin doesn't support the sample format",
				arg);
		break;
	case IP_ERROR_NOT_OPTION:
		snprintf(buffer, sizeof(buffer),
				"%s: no such option", arg);
		break;
	case IP_ERROR_INTERNAL:
		snprintf(buffer, sizeof(buffer), "%s: internal error", arg);
		break;
	case IP_ERROR_SUCCESS:
	default:
		snprintf(buffer, sizeof(buffer),
				"%s: this is not an error (%d), this is a bug",
				arg, rc);
		break;
	}
	return xstrdup(buffer);
}

char **ip_get_supported_extensions(void)
{
	struct ip *ip;
	char **exts;
	int i, size;
	int count = 0;

	size = 8;
	exts = xnew(char *, size);
	ip_rdlock();
	list_for_each_entry(ip, &ip_head, node) {
		const char * const *e = ip->extensions;

		for (i = 0; e[i]; i++) {
			if (count == size - 1) {
				size *= 2;
				exts = xrenew(char *, exts, size);
			}
			exts[count++] = xstrdup(e[i]);
		}
	}
	ip_unlock();
	exts[count] = NULL;
	qsort(exts, count, sizeof(char *), strptrcmp);
	return exts;
}

void ip_dump_plugins(void)
{
	struct ip *ip;
	int i;

	printf("Input Plugins: %s\n", plugin_dir);
	ip_rdlock();
	list_for_each_entry(ip, &ip_head, node) {
		printf("  %s:\n    Default Priority: %d\n    File Types:", ip->name, ip->priority);
		for (i = 0; ip->extensions[i]; i++)
			printf(" %s", ip->extensions[i]);
		printf("\n    MIME Types:");
		for (i = 0; ip->mime_types[i]; i++)
			printf(" %s", ip->mime_types[i]);
		printf("\n");
	}
	ip_unlock();
}

/*
 * PgBouncer - Lightweight connection pooler for PostgreSQL.
 * 
 * Copyright (c) 2007 Marko Kreen, Skype Technologies OÜ
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Random small utility functions
 */

#include "bouncer.h"

#include "md5.h"

void *zmalloc(size_t len)
{
	void *p = malloc(len);
	if (p)
		memset(p, 0, len);
	return p;
}

/*
 * Generic logging
 */

static void render_time(char *buf, int max)
{
	struct tm tm;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(buf, max, "%Y-%m-%d %H:%M:%S", &tm);
}

static void _log_write(const char *pfx, const char *msg)
{
	char buf[1024];
	char tbuf[64];
	int len;
	render_time(tbuf, sizeof(tbuf));
	len = snprintf(buf, sizeof(buf), "%s %u %s %s\n",
			tbuf, (unsigned)getpid(), pfx, msg);
	if (cf_logfile) {
		int fd = open(cf_logfile, O_CREAT | O_APPEND | O_WRONLY, 0644);
		if (fd > 0) {
			safe_write(fd, buf, len);
			safe_close(fd);
		}
	}
	if (!cf_daemon)
		fprintf(stderr, "%s", buf);
}

static void _log(const char *pfx, const char *fmt, va_list ap)
{
	char buf[1024];
	vsnprintf(buf, sizeof(buf), fmt, ap);
	_log_write(pfx, buf);
}

void _fatal(const char *file, int line, const char *func,
	    bool do_exit, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	snprintf(buf, sizeof(buf),
		 "@%s:%d in function %s(): %s",
		 file, line, func, fmt);

	va_start(ap, fmt);
	_log("FATAL", buf, ap);
	va_end(ap);
	if (do_exit)
		exit(1);
}

void _fatal_perror(const char *file, int line, const char *func,
		   const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	_fatal(file, line, func, false, "%s: %s", buf, strerror(errno));
}

/*
 * generic logging
 */
void log_level(const char *pfx, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	_log(pfx, fmt, ap);
	va_end(ap);
}

/*
 * Logging about specific PgSocket
 */

void
slog_level(const char *pfx, const PgSocket *sock, const char *fmt, ...)
{
	char buf1[1024];
	char buf2[1024];
	char *user, *db, *host;
	int port;
	va_list ap;

	db = sock->pool ? sock->pool->db->name : "(nodb)";
	user = sock->auth_user ? sock->auth_user->name : "(nouser)";
	if (sock->addr.is_unix) {
		host = "unix";
	} else {
		host = inet_ntoa(sock->addr.ip_addr);
	}
	port = sock->addr.port;

	va_start(ap, fmt);
	vsnprintf(buf1, sizeof(buf1), fmt, ap);
	va_end(ap);

	snprintf(buf2, sizeof(buf2), "%c-%p: %s/%s@%s:%d %s",
			is_server_socket(sock) ? 'S' : 'C',
			sock, db, user, host, port, buf1);

	_log_write(pfx, buf2);
}


/*
 * Wrappers for read/write/recv/send that survive interruptions.
 */

int safe_read(int fd, void *buf, int len)
{
	int res;
loop:
	res = read(fd, buf, len);
	if (res < 0 && errno == EINTR)
		goto loop;
	return res;
}

int safe_write(int fd, const void *buf, int len)
{
	int res;
loop:
	res = write(fd, buf, len);
	if (res < 0 && errno == EINTR)
		goto loop;
	return res;
}

int safe_recv(int fd, void *buf, int len, int flags)
{
	int res;
loop:
	res = recv(fd, buf, len, flags);
	if (res < 0 && errno == EINTR)
		goto loop;
	if (res < 0)
		log_noise("safe_recv(%d, %d) = %s", fd, len, strerror(errno));
	else if (cf_verbose > 2)
		log_noise("safe_recv(%d, %d) = %d", fd, len, res);
	return res;
}

int safe_send(int fd, const void *buf, int len, int flags)
{
	int res;
loop:
	res = send(fd, buf, len, flags);
	if (res < 0 && errno == EINTR)
		goto loop;
	if (res < 0)
		log_noise("safe_send(%d, %d) = %s", fd, len, strerror(errno));
	else if (cf_verbose > 2)
		log_noise("safe_send(%d, %d) = %d", fd, len, res);
	return res;
}

int safe_close(int fd)
{
	int res;
loop:
	/* by manpage, the close() could be interruptable
	   although it seems that at least in linux it cannot happen */
	res = close(fd);
	if (res < 0 && errno == EINTR)
		goto loop;
	return res;
}

int safe_recvmsg(int fd, struct msghdr *msg, int flags)
{
	int res;
loop:
	res = recvmsg(fd, msg, flags);
	if (res < 0 && errno == EINTR)
		goto loop;
	if (res < 0)
		log_warning("safe_recvmsg(%d, msg, %d) = %s", fd, flags, strerror(errno));
	else if (cf_verbose > 2)
		log_noise("safe_recvmsg(%d, msg, %d) = %d", fd, flags, res);
	return res;
}

int safe_sendmsg(int fd, const struct msghdr *msg, int flags)
{
	int res;
	int msgerr_count = 0;
loop:
	res = sendmsg(fd, msg, flags);
	if (res < 0 && errno == EINTR)
		goto loop;

	if (res < 0) {
		log_warning("safe_sendmsg(%d, msg[%d,%d], %d) = %s", fd,
			    msg->msg_iov[0].iov_len,
			    msg->msg_controllen,
			    flags, strerror(errno));

		/* with ancillary data on blocking socket OSX returns
		 * EMSGSIZE instead of blocking.  try to solve it by waiting */
		if (errno == EMSGSIZE && msgerr_count < 20) {
			struct timeval tv = {1, 0};
			log_warning("trying to sleep a bit");
			select(0, NULL, NULL, NULL, &tv);
			msgerr_count++;
			goto loop;
		}
	} else if (cf_verbose > 2)
		log_noise("safe_sendmsg(%d, msg, %d) = %d", fd, flags, res);
	return res;
}

/*
 * Load a file into malloc()-ed C string.
 */

char *load_file(const char *fn)
{
	struct stat st;
	char *buf = NULL;
	int res, fd;

	res = stat(fn, &st);
	if (res < 0) {
		log_error("%s: %s", fn, strerror(errno));
		goto load_error;
	}

	buf = malloc(st.st_size + 1);
	if (!buf)
		goto load_error;

	if ((fd = open(fn, O_RDONLY)) < 0) {
		log_error("%s: %s", fn, strerror(errno));
		goto load_error;
	}

	if ((res = safe_read(fd, buf, st.st_size)) < 0) {
		log_error("%s: %s", fn, strerror(errno));
		goto load_error;
	}

	safe_close(fd);
	buf[st.st_size] = 0;

	return buf;

load_error:
	if (buf != NULL)
		free(buf);
	return NULL;
}

/*
 * PostgreSQL MD5 "encryption".
 */

static void hash2hex(const uint8 *hash, char *dst)
{
        int i;
        static const char hextbl [] = "0123456789abcdef";
        for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
                *dst++ = hextbl[hash[i] >> 4];
                *dst++ = hextbl[hash[i] & 15];
        }
        *dst = 0;
}

bool pg_md5_encrypt(const char *part1,
		    const char *part2, size_t part2len,
		    char *dest)
{
        MD5_CTX ctx;
        uint8 hash[MD5_DIGEST_LENGTH];

        MD5_Init(&ctx);
        MD5_Update(&ctx, part1, strlen(part1));
        MD5_Update(&ctx, part2, part2len);
        MD5_Final(hash, &ctx);

	memcpy(dest, "md5", 3);
        hash2hex(hash, dest + 3);

	memset(hash, 0, sizeof(*hash));
	return true;
}

/* wrapped for getting random bytes */
bool get_random_bytes(uint8 *dest, int len)
{
	int i;
	for (i = 0; i < len; i++)
		dest[i] = random() & 255;
	return len;
}

/*
 * high-precision time
 */

static usec_t get_time_usec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (usec_t)tv.tv_sec * USEC + tv.tv_usec;
}

/*
 * cache time, as we don't need sub-second precision
 */
static usec_t time_cache = 0;

usec_t get_cached_time(void)
{
	if (!time_cache)
		time_cache = get_time_usec();
	return time_cache;
}

void reset_time_cache(void)
{
	time_cache = 0;
}

void socket_set_nonblocking(int fd, int val)
{
	int flags, res;

	/* get old flags */
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		fatal_perror("fcntl(F_GETFL)");

	/* flip O_NONBLOCK */
	if (val)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	/* set new flags */
	res = fcntl(fd, F_SETFL, flags);
	if (res < 0)
		fatal_perror("fcntl(F_SETFL)");
}

/* set needed socket options */
void tune_socket(int sock, bool is_unix)
{
	int res;
	int val;

	/* close fd on exec */
	res = fcntl(sock, F_SETFD, FD_CLOEXEC);
	if (res < 0)
		fatal_perror("fcntl FD_CLOEXEC");

	/* when no data available, return EAGAIN instead blocking */
	socket_set_nonblocking(sock, 1);

#ifdef SO_NOSIGPIPE
	/* disallow SIGPIPE, if possible */
	val = 1;
	res = setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
	if (res < 0)
		fatal_perror("setsockopt SO_NOSIGPIPE");
#endif

	/*
	 * Following options are for network sockets
	 */
	if (is_unix)
		return;

	/* the keepalive stuff needs some poking before enbling */
	if (cf_tcp_keepalive) {
		/* turn on socket keepalive */
		val = 1;
		res = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
		if (res < 0)
			fatal_perror("setsockopt SO_KEEPALIVE");
#ifdef __linux__
		/* set count of keepalive packets */
		if (cf_tcp_keepcnt > 0) {
			val = cf_tcp_keepcnt;
			res = setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val));
			if (res < 0)
				fatal_perror("setsockopt TCP_KEEPCNT");
		}
		/* how long the connection can stay idle before sending keepalive pkts */
		if (cf_tcp_keepidle) {
			val = cf_tcp_keepidle;
			res = setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val));
			if (res < 0)
				fatal_perror("setsockopt TCP_KEEPIDLE");
		}
		/* time between packets */
		if (cf_tcp_keepintvl) {
			val = cf_tcp_keepintvl;
			res = setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val));
			if (res < 0)
				fatal_perror("setsockopt TCP_KEEPINTVL");
		}
#else
#ifdef TCP_KEEPALIVE
		if (cf_tcp_keepidle) {
			val = cf_tcp_keepidle;
			res = setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val));
			if (res < 0)
				fatal_perror("setsockopt TCP_KEEPALIVE");
		}
#endif
#endif
	}

	/* set in-kernel socket buffer size */
	if (cf_tcp_socket_buffer) {
		val = cf_tcp_socket_buffer;
		res = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
		if (res < 0)
			fatal_perror("setsockopt SO_SNDBUF");
		val = cf_tcp_socket_buffer;
		res = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
		if (res < 0)
			fatal_perror("setsockopt SO_RCVBUF");
	}

	/*
	 * Turn off kernel buffering, each send() will be one packet.
	 */
	val = 1;
	res = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
	if (res < 0)
		fatal_perror("setsockopt TCP_NODELAY");
}

/*
 * Find a string in comma-separated list.
 *
 * It does not support space inside tokens.
 */
bool strlist_contains(const char *liststr, const char *str)
{
	int c, len = strlen(str);
	const char *p, *listpos = liststr;
	
loop:
	/* find string fragment, later check if actual token */
	p = strstr(listpos, str);
	if (p == NULL)
		return false;

	/* move listpos further */
	listpos = p + len;
	/* survive len=0 and avoid unneccesary compare */
	if (*listpos)
		listpos++;

	/* check previous symbol */
	if (p > liststr) {
		c = *(p - 1);
		if (!isspace(c) && c != ',')
			goto loop;
	}

	/* check following symbol */
	c = p[len];
	if (c != 0 && !isspace(c) && c != ',')
		goto loop;

	return true;
}

const char *format_date(usec_t uval)
{
	static char buf[128];
	time_t tval = uval / USEC;
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&tval));
	return buf;
}

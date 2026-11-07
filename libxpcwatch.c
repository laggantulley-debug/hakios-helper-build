/*
 * libxpcwatch.dylib —— 把 XPC 同步请求的目标名和 payload 打出来（只读，改无）。
 * 版本 2：payload 的换行转成空格、最长打 1800 字符，能看到键名。
 */
typedef void *xpc_object_t;
typedef void *xpc_connection_t;
typedef unsigned long size_t;
typedef unsigned long uint64_t_;

extern int write(int, const void *, unsigned long);
extern xpc_object_t xpc_connection_send_message_with_reply_sync(xpc_connection_t, xpc_object_t);
extern void xpc_connection_send_message(xpc_connection_t, xpc_object_t);
extern void xpc_connection_send_barrier(xpc_connection_t, void (^)(void));
extern const char *xpc_connection_get_name(xpc_connection_t);
extern char *xpc_copy_description(xpc_object_t);
extern void free(void *);
extern void *dlsym(void *, const char *);
extern long syscall(int, ...);

#define RTLD_NEXT ((void *)-1)

static int gCount = 0;

static unsigned long
slen(const char *s)
{
	unsigned long n = 0;
	while (s && s[n]) n++;
	return n;
}

static void
emit(const char *a, const char *b, const char *c, unsigned long cap)
{
	char buf[2048];
	unsigned long n = 0;
	const char *parts[3] = { a, b, c };
	for (int k = 0; k < 3; k++) {
		const char *p = parts[k];
		if (!p) continue;
		unsigned long lim = slen(p);
		if (lim > cap) lim = cap;
		for (unsigned long i = 0; i < lim && n < sizeof(buf) - 2; i++) {
			char ch = p[i];
			buf[n++] = (ch == '\n' || ch == '\r') ? ' ' : ch;
		}
	}
	buf[n++] = '\n';
	write(2, buf, n);
}

static xpc_object_t
my_send_sync(xpc_connection_t conn, xpc_object_t msg)
{
	const char *name = conn ? xpc_connection_get_name(conn) : "(null)";
	char *desc = msg ? xpc_copy_description(msg) : 0;
	if (gCount < 30) {
		emit("[xpcwatch] -> SYNC name=", name ? name : "(null)", desc ? desc : "(no-desc)", 600);
		gCount++;
	}
	if (desc) free(desc);
	return xpc_connection_send_message_with_reply_sync(conn, msg);
}

struct interposer { const void *replacement; const void *original; };

__attribute__((used, section("__DATA,__interpose")))
static const struct interposer g_interposers[] = {
	{ (const void *)my_send_sync, (const void *)xpc_connection_send_message_with_reply_sync },
};

__attribute__((constructor)) static void
xpcwatch_init(void)
{
	static const char msg[] = "[xpcwatch] loaded (v2: full payload, newlines flattened)\n";
	write(2, msg, sizeof(msg) - 1);
}

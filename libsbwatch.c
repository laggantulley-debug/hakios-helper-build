/*
 * libsbwatch.dylib —— SpringBoard 里的看门狗：
 *  1) 每 20 秒把进程内每个线程的 PC/LR/SP 打到 stderr（串口）
 *  2) 用 dyld 的 __interpose 拦截 mach_msg，把「接收等待」的消息号/端口打出来，
 *     用来判断主线程到底卡在等谁的回包
 *
 * 只读、不改行为：拦截函数原样转调原始 mach_msg。
 */
typedef unsigned int   natural_t;
typedef unsigned long  uint64_t_;

extern natural_t mach_task_self_;
extern int  task_threads(natural_t task, natural_t **threads, natural_t *count);
extern int  thread_get_state(natural_t thread, int flavor, unsigned int *state, unsigned int *count);
extern int  mach_port_deallocate(natural_t task, natural_t name);
extern long write(int, const void *, unsigned long);
extern int  pthread_create(unsigned long *, const void *, void *(*)(void *), void *);
extern int  usleep(unsigned int);
extern const char *_dyld_get_shared_cache_range(unsigned long *len);
extern int  mach_msg(void *msg, unsigned int option, unsigned int send_size, unsigned int rcv_size,
                     unsigned int rcv_name, unsigned int timeout, unsigned int notify);

#define ARM_THREAD_STATE64 6
#define ARM_THREAD_STATE64_COUNT 68
#define MACH_RCV_MSG 0x00000002

typedef struct {
	unsigned int msgh_bits;
	unsigned int msgh_size;
	unsigned int msgh_remote_port;
	unsigned int msgh_local_port;
	unsigned int msgh_voucher_port;
	int          msgh_id;
} msg_hdr_t;

static int
fmt_hex(char *p, unsigned long v)
{
	static const char *h = "0123456789abcdef";
	char tmp[24]; int n = 0, k = 0;
	p[k++] = '0'; p[k++] = 'x';
	if (v == 0) { p[k++] = '0'; return k; }
	while (v) { tmp[n++] = h[v & 15]; v >>= 4; }
	while (n) p[k++] = tmp[--n];
	return k;
}
static int
fmt_dec(char *p, long v)
{
	char tmp[24]; int n = 0, k = 0;
	if (v == 0) { p[0] = '0'; return 1; }
	if (v < 0) { p[k++] = '-'; v = -v; }
	while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
	while (n) p[k++] = tmp[--n];
	return k;
}

/* ---- 1) 线程状态 ---- */
static void
dump_threads(int round)
{
	unsigned long len = 0, slide = 0;
	const char *cache = _dyld_get_shared_cache_range(&len);
	if (cache != 0) slide = (unsigned long)cache - 0x180000000UL;

	char buf[256];
	int n = 0;
	const char *a = "[sbwatch] round ";
	for (const char *q = a; *q; q++) buf[n++] = *q;
	n += fmt_dec(buf + n, round);
	const char *b = " slide=";
	for (const char *q = b; *q; q++) buf[n++] = *q;
	n += fmt_hex(buf + n, slide);
	buf[n++] = '\n';
	write(2, buf, (unsigned long)n);

	natural_t *threads = 0;
	natural_t count = 0;
	if (task_threads(mach_task_self_, &threads, &count) != 0) return;
	for (natural_t i = 0; i < count && i < 16; i++) {
		unsigned int st[ARM_THREAD_STATE64_COUNT];
		unsigned int cnt = ARM_THREAD_STATE64_COUNT;
		if (thread_get_state(threads[i], ARM_THREAD_STATE64, st, &cnt) != 0) continue;
		unsigned long *r = (unsigned long *)st;
		n = 0;
		const char *c = "[sbwatch]   t";
		for (const char *q = c; *q; q++) buf[n++] = *q;
		n += fmt_dec(buf + n, i);
		const char *d = " pc=";
		for (const char *q = d; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, r[32]);
		const char *e = " lr=";
		for (const char *q = e; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, r[30]);
		const char *f = " sp=";
		for (const char *q = f; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, r[31]);
		buf[n++] = '\n';
		write(2, buf, (unsigned long)n);
		mach_port_deallocate(mach_task_self_, threads[i]);
	}
}

static void *
watchdog(void *arg)
{
	(void)arg;
	for (int round = 1; round <= 60; round++) {
		usleep(20 * 1000 * 1000);
		dump_threads(round);
	}
	return 0;
}

/* ---- 2) mach_msg 拦截 ---- */
static int g_msg_logged;
static int g_elapsed;

static int
my_mach_msg(void *msg, unsigned int option, unsigned int send_size, unsigned int rcv_size,
            unsigned int rcv_name, unsigned int timeout, unsigned int notify)
{
	int is_recv = (option & MACH_RCV_MSG) != 0;
	if (is_recv && g_msg_logged < 40) {
		msg_hdr_t *h = (msg_hdr_t *)msg;
		char buf[160];
		int n = 0;
		const char *a = "[sbwatch] mach_msg recv_id=";
		for (const char *q = a; *q; q++) buf[n++] = *q;
		n += fmt_dec(buf + n, h ? h->msgh_id : -1);
		const char *b = " bits=";
		for (const char *q = b; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, h ? h->msgh_bits : 0);
		const char *c = " local=";
		for (const char *q = c; *q; q++) buf[n++] = *q;
		n += fmt_dec(buf + n, h ? (long)h->msgh_local_port : -1);
		const char *d = " rcvname=";
		for (const char *q = d; *q; q++) buf[n++] = *q;
		n += fmt_dec(buf + n, (long)rcv_name);
		const char *e = " to_ms=";
		for (const char *q = e; *q; q++) buf[n++] = *q;
		n += fmt_dec(buf + n, (long)timeout);
		buf[n++] = '\n';
		write(2, buf, (unsigned long)n);
		g_msg_logged++;
	}
	return mach_msg(msg, option, send_size, rcv_size, rcv_name, timeout, notify);
}

struct interposer { const void *replacement; const void *original; };
__attribute__((used, section("__DATA,__interpose")))
static const struct interposer g_interposers[] = {
	{ (const void *)my_mach_msg, (const void *)mach_msg },
};

__attribute__((constructor)) static void
sbwatch_init(void)
{
	unsigned long th = 0;
	static char init_msg[] = "[sbwatch] loaded\n";
	write(2, init_msg, sizeof(init_msg) - 1);
	pthread_create(&th, 0, watchdog, 0);
}

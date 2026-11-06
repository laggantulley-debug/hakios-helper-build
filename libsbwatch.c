/*
 * libsbwatch.dylib —— SpringBoard 里的小看门狗：每隔 N 秒把进程内每个线程的
 * PC / LR / SP 打到 stderr（也就是串口控制台），用来判断主线程卡在哪个函数里。
 *
 * 用 mach 的 task_threads + thread_get_state 读取，不注入信号、不暂停进程。
 * 输出里同时带 dyld 共享缓存的 slide，方便离线用缓存镜像表把 PC 解析成库名+偏移。
 */
typedef unsigned int   mach_port_t;
typedef unsigned int   natural_t;
typedef struct { natural_t a, b; } mach_msg_type_number_t_wrap;
typedef unsigned long  uint64_t_;

extern mach_port_t mach_task_self_;
extern int  task_threads(mach_port_t task, mach_port_t **threads, natural_t *count);
extern int  thread_get_state(mach_port_t thread, int flavor, unsigned int *state, unsigned int *count);
extern int  mach_port_deallocate(mach_port_t task, mach_port_t name);
extern long write(int, const void *, unsigned long);
extern int  pthread_create(unsigned long *, const void *, void *(*)(void *), void *);
extern int  usleep(unsigned int);
extern const char *_dyld_get_shared_cache_range(unsigned long *len);

#define ARM_THREAD_STATE64 6
#define ARM_THREAD_STATE64_COUNT 68

static int
fmt_u64(char *p, unsigned long v)
{
	char tmp[24]; int n = 0, k = 0;
	if (v == 0) { p[0] = '0'; return 1; }
	while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
	while (n) p[k++] = tmp[--n];
	return k;
}
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

static void
dump_threads(int round)
{
	unsigned long len = 0;
	unsigned long slide = 0;
	const char *cache = _dyld_get_shared_cache_range(&len);
	if (cache != 0) slide = (unsigned long)cache - 0x180000000UL;

	char buf[256];
	int n = 0;
	const char *a = "[sbwatch] round ";
	for (const char *q = a; *q; q++) buf[n++] = *q;
	n += fmt_u64(buf + n, (unsigned long)round);
	const char *b = " slide=";
	for (const char *q = b; *q; q++) buf[n++] = *q;
	n += fmt_hex(buf + n, slide);
	buf[n++] = '\n';
	write(2, buf, (unsigned long)n);

	mach_port_t *threads = 0;
	natural_t count = 0;
	if (task_threads(mach_task_self_, &threads, &count) != 0) {
		write(2, "[sbwatch] task_threads failed\n", 30);
		return;
	}
	for (natural_t i = 0; i < count && i < 12; i++) {
		unsigned int st[ARM_THREAD_STATE64_COUNT];
		unsigned int cnt = ARM_THREAD_STATE64_COUNT;
		if (thread_get_state(threads[i], ARM_THREAD_STATE64, st, &cnt) != 0) continue;
		unsigned long *r = (unsigned long *)st;
		unsigned long sp = r[31], pc = r[32], lr = r[30];

		n = 0;
		const char *c = "[sbwatch]   thread ";
		for (const char *q = c; *q; q++) buf[n++] = *q;
		n += fmt_u64(buf + n, (unsigned long)i);
		const char *d = " pc=";
		for (const char *q = d; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, pc);
		const char *e = " lr=";
		for (const char *q = e; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, lr);
		const char *f = " sp=";
		for (const char *q = f; *q; q++) buf[n++] = *q;
		n += fmt_hex(buf + n, sp);
		buf[n++] = '\n';
		write(2, buf, (unsigned long)n);
		mach_port_deallocate(mach_task_self_, threads[i]);
	}
}

static void *
watchdog(void *arg)
{
	(void)arg;
	for (int round = 1; round <= 20; round++) {
		usleep(20 * 1000 * 1000);
		dump_threads(round);
	}
	return 0;
}

__attribute__((constructor)) static void
sbwatch_init(void)
{
	unsigned long th = 0;
	static char init_msg[] = "[sbwatch] loaded\n";
	write(2, init_msg, sizeof(init_msg) - 1);
	pthread_create(&th, 0, watchdog, 0);
}

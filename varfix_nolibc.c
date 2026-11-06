/* varfix —— 纯系统调用实现（不需要 libc / dyld），按 /etc/var.skel 建好 /private/var。 */
typedef unsigned long size_t_;

#define SYS_exit    1
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_chmod   15
#define SYS_chown   16
#define SYS_symlink 57
#define SYS_mkdir   136

static long
sys5(long n, long a, long b, long c, long d, long e)
{
	register long x16 __asm__("x16") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	__asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory", "cc");
	return x0;
}
#define sys(n,a,b,c) sys5(n,(long)(a),(long)(b),(long)(c),0,0)

static long
strlen_(const char *s)
{
	long n = 0;
	while (s[n]) n++;
	return n;
}
static void
puts_(const char *s)
{
	sys(SYS_write, 2, s, strlen_(s));
}
static int
streq_n(const char *a, const char *b, int n)
{
	for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
	return 1;
}
static long
atoi_(const char *s, int base)
{
	long v = 0;
	while (*s == ' ' || *s == '\t') s++;
	while (*s) {
		char c = *s;
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else break;
		v = v * base + d;
		s++;
	}
	return v;
}
static char *
find_tab(char *p)
{
	while (*p && *p != '\t') p++;
	return *p ? p : 0;
}

static char buf[16384];

void
_start(void)
{
	long fd = sys(SYS_open, "/etc/var.skel", 0, 0);
	long made = 0, links = 0, n = 0;
	if (fd >= 0) {
		n = sys(SYS_read, fd, buf, sizeof(buf) - 1);
		sys(SYS_close, fd, 0, 0);
	}
	if (n > 0) {
		buf[n] = 0;
		char *line = buf;
		while (*line) {
			char *end = line;
			while (*end && *end != '\n') end++;
			if (*end) *end = 0;
			if (line[0] == 'd' && line[1] == '\t') {
				char *p1 = line + 2;
				char *t1 = find_tab(p1); if (!t1) goto next;
				*t1 = 0;
				char *p2 = t1 + 1;
				char *t2 = find_tab(p2); if (!t2) goto next;
				*t2 = 0;
				char *p3 = t2 + 1;
				char *t3 = find_tab(p3); if (!t3) goto next;
				*t3 = 0;
				char *path = t3 + 1;
				long mode = atoi_(p1, 8), uid = atoi_(p2, 10), gid = atoi_(p3, 10);
				sys(SYS_mkdir, path, 0777, 0);
				sys(SYS_chmod, path, mode, 0);
				sys(SYS_chown, path, uid, gid);
				made++;
			} else if (line[0] == 'l' && line[1] == '\t') {
				char *p1 = line + 2;
				char *t1 = find_tab(p1); if (!t1) goto next;
				*t1 = 0;
				char *path = t1 + 1;
				sys(SYS_symlink, p1, path, 0);
				links++;
			}
next:
			if (!*end) break;
			line = end + 1;
		}
	}
	sys(SYS_chmod, "/private/var", 0777, 0);

	{
		char msg[64];
		int l = 0;
		const char *a = "[varfix-nolibc] dirs=";
		for (const char *p = a; *p; p++) msg[l++] = *p;
		if (made >= 10) msg[l++] = (char)('0' + made / 10);
		msg[l++] = (char)('0' + made % 10);
		const char *b = " links=";
		for (const char *p = b; *p; p++) msg[l++] = *p;
		if (links >= 10) msg[l++] = (char)('0' + links / 10);
		msg[l++] = (char)('0' + links % 10);
		msg[l++] = '\n';
		sys(SYS_write, 2, msg, l);
	}
	sys(SYS_exit, 0, 0, 0);
	for (;;) { }
}

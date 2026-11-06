/* libvarfix.dylib —— 注入 root 进程后按 /etc/var.skel 建 /private/var。
 * 用 -undefined dynamic_lookup 编译：不需要 iOS SDK，符号由 dyld 在运行时绑定。 */
typedef unsigned long ulong;

extern int open(const char *, int, ...);
extern long read(int, void *, ulong);
extern int close(int);
extern int mkdir(const char *, unsigned);
extern int chmod(const char *, unsigned);
extern int chown(const char *, unsigned, unsigned);
extern long symlink(const char *, const char *);
extern long write(int, const void *, ulong);
extern unsigned getuid(void);
extern char *strchr(const char *, int);
extern int atoi(const char *);
extern long strtol(const char *, char **, int);

#define SKEL "/etc/var.skel"

static int split_tabs(char *line, char **out, int max)
{
	int n = 0;
	char *p = line;
	while (n < max) {
		out[n++] = p;
		char *t = strchr(p, '\t');
		if (!t) break;
		*(char *)t = 0;
		p = t + 1;
	}
	return n;
}

__attribute__((constructor)) static void
varfix_init(void)
{
	static char buf[16384];
	int made = 0, links = 0;
	int fd = open(SKEL, 0);
	if (fd >= 0) {
		long n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			buf[n] = 0;
			for (char *line = buf; *line; ) {
				char *end = strchr(line, '\n'), *f[6];
				if (end) *(char *)end = 0;
				int c = split_tabs(line, f, 6);
				if (c >= 5 && f[0][0] == 'd') {
					int mode = (int)strtol(f[1], 0, 8);
					mkdir(f[4], 0777);
					chmod(f[4], (unsigned)mode);
					chown(f[4], (unsigned)atoi(f[2]), (unsigned)atoi(f[3]));
					made++;
				} else if (c >= 3 && f[0][0] == 'l') {
					symlink(f[1], f[2]);
					links++;
				}
				if (!end) break;
				line = end + 1;
			}
		}
	}
	chmod("/private/var", 0777);

	{
		char msg[96];
		int l = 0;
		const char *a = "[libvarfix] uid=";
		for (const char *p = a; *p; p++) msg[l++] = *p;
		unsigned u = getuid();
		if (u == 0) msg[l++] = '0';
		else { char t[12]; int k = 0; while (u) { t[k++] = (char)('0' + u % 10); u /= 10; } while (k) msg[l++] = t[--k]; }
		const char *b = " dirs=";
		for (const char *p = b; *p; p++) msg[l++] = *p;
		if (made >= 100) { msg[l++] = '1'; msg[l++] = '0'; msg[l++] = '0'; msg[l++] = '+'; }
		else { if (made >= 10) msg[l++] = (char)('0' + made / 10); msg[l++] = (char)('0' + made % 10); }
		const char *c2 = " links=";
		for (const char *p = c2; *p; p++) msg[l++] = *p;
		if (links >= 10) msg[l++] = (char)('0' + links / 10);
		msg[l++] = (char)('0' + links % 10);
		msg[l++] = '\n';
		write(2, msg, (ulong)l);
	}
}

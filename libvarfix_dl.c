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
extern long lseek(int, long, int);
extern int fchmod(int, unsigned);
extern int fchown(int, unsigned, unsigned);

#define SKEL "/etc/var.skel"

static char buf2[16384];

static int
copy_one(const char *src, const char *dst, int mode, unsigned uid, unsigned gid)
{
	char tmp[1024];
	unsigned n = 0;
	while (src[n] && n < sizeof(tmp) - 1) { tmp[n] = src[n]; n++; }
	tmp[n] = 0;

	int in = open(tmp, 0);
	if (in < 0) return -1;
	int out = open(dst, 1 | 0x200 /*O_WRONLY|O_CREAT*/, 0644);
	if (out < 0) { close(in); return -1; }          /* 已存在就不覆盖 */
	long got;
	while ((got = read(in, buf2, sizeof(buf2))) > 0) {
		long done = 0;
		while (done < got) {
			long w = write(out, buf2 + done, (ulong)(got - done));
			if (w <= 0) { close(out); close(in); return -1; }
			done += w;
		}
	}
	close(out);
	close(in);
	chmod(dst, (unsigned)mode);
	chown(dst, uid, gid);
	return 0;
}

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
	int made = 0, links = 0, files = 0;
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
				} else if (c >= 6 && f[0][0] == 'f') {
					copy_one(f[5], f[4], (int)strtol(f[1], 0, 8), (unsigned)atoi(f[2]), (unsigned)atoi(f[3]));
					files++;
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
		const char *c2 = " files=";
		for (const char *p = c2; *p; p++) msg[l++] = *p;
		msg[l++] = (char)('0' + files % 10);
		const char *c2b = " links=";
		for (const char *p = c2; *p; p++) msg[l++] = *p;
		if (links >= 10) msg[l++] = (char)('0' + links / 10);
		msg[l++] = (char)('0' + links % 10);
		msg[l++] = '\n';
		write(2, msg, (ulong)l);
	}
}

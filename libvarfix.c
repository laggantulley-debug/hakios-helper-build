/*
 * libvarfix.dylib —— 按 /etc/var.skel 建好 /private/var 骨架。
 *
 * 为什么不做成 launchd job：系统域的 launchd 在我们的卷里会把这类没有 MachServices 的 job
 * 丢进 user 域（uid 501），那样 chmod/chown 自己也会被拒。改成注入进一个 **root** 进程
 * （inject.conf 里映射到 logd），构造器里做事，权限就没问题了。
 *
 * 只改内存文件系统，不碰分区。
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SKEL "/etc/var.skel"

static int split_tabs(char *line, char **out, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        out[n++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = 0;
        p = t + 1;
    }
    return n;
}

__attribute__((constructor)) static void varfix_init(void) {
    static char buf[16384];
    int made = 0, links = 0;

    int fd = open(SKEL, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            for (char *line = buf; *line; ) {
                char *end = strchr(line, '\n'), *f[6];
                if (end) *end = 0;
                int c = split_tabs(line, f, 6);
                if (c >= 5 && f[0][0] == 'd') {
                    mode_t mode = (mode_t)strtol(f[1], NULL, 8);
                    if (mkdir(f[4], 0777) != 0 && errno != EEXIST) { }
                    chmod(f[4], mode);
                    chown(f[4], (uid_t)atoi(f[2]), (gid_t)atoi(f[3]));
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
    chmod("/private/var", 01777);

    char msg[128];
    int len = 0;
    const char *a = "[libvarfix] uid=";
    for (const char *p = a; *p; p++) msg[len++] = *p;
    int u = (int)getuid();
    char tmp[12]; int t = 0;
    if (u == 0) tmp[t++] = '0';
    while (u > 0) { tmp[t++] = (char)('0' + u % 10); u /= 10; }
    while (t > 0) msg[len++] = tmp[--t];
    const char *b = " dirs=";
    for (const char *p = b; *p; p++) msg[len++] = *p;
    if (made >= 100) { msg[len++]='1'; msg[len++]='0'; msg[len++]='0'; msg[len++]='+'; }
    else { if (made >= 10) msg[len++] = (char)('0' + made/10); msg[len++] = (char)('0' + made%10); }
    const char *c2 = " links=";
    for (const char *p = c2; *p; p++) msg[len++] = *p;
    if (links >= 10) msg[len++] = (char)('0' + links/10);
    msg[len++] = (char)('0' + links%10);
    msg[len++] = '\n';
    write(2, msg, (size_t)len);
}

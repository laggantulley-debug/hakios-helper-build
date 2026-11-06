/*
 * varfix —— 按 /etc/var.skel 把 /private/var 的内存文件系统建好（属主/权限都照模板）。
 *
 * 为什么需要：内核起来后 init 会 `mount ramfs /private/var`，得到空的内存文件系统、
 * 属主 root、模式 0755。SpringBoard 以 mobile(501) 运行，写 /var/mobile/** 全部 EACCES，
 * 于是卡在早期初始化。上游 init.c 的 mount_writable() 就是读 var.skel 逐个
 * mkdir + chmod + chown（并自检"建了 50 个以上目录"），这里照做。
 *
 * 模板格式（TAB 分隔）：
 *   d <mode octal> <uid> <gid> <path>
 *   l <target> <path>
 *
 * 退出码返回 1，配合 launchd 的 KeepAlive.SuccessfulExit=false 反复执行，
 * 所以无论它比 SpringBoard 早还是晚，都会在下一秒补正。
 *
 * 只改内存文件系统，不碰任何分区。
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

int main(void) {
    static char buf[16384];
    int fd = open(SKEL, O_RDONLY);
    if (fd < 0) {
        write(2, "[varfix] no " SKEL "\n", 25);
        return 1;
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1;
    buf[n] = 0;

    int made = 0, links = 0;
    for (char *line = buf; *line; ) {
        char *end = strchr(line, '\n'), *f[6];
        if (end) *end = 0;
        int c = split_tabs(line, f, 6);
        if (c >= 5 && f[0][0] == 'd') {
            mode_t mode = (mode_t)strtol(f[1], NULL, 8);
            if (mkdir(f[4], 0777) != 0 && errno != EEXIST) { /* continue anyway */ }
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

    /* 挂载点本身也放开，避免任何 daemon 想直接在 /private/var 下建东西时被拒 */
    chmod("/private/var", 01777);

    char msg[96];
    int len = 0;
    msg[len++] = '[';
    const char *t = "varfix] dirs=";
    for (const char *p = t; *p; p++) msg[len++] = *p;
    if (made >= 100) { msg[len++] = '1'; msg[len++] = '0'; msg[len++] = '0'; msg[len++] = '+'; }
    else { if (made >= 10) msg[len++] = '0' + made / 10; msg[len++] = '0' + made % 10; }
    const char *t2 = " links=";
    for (const char *p = t2; *p; p++) msg[len++] = *p;
    if (links >= 10) msg[len++] = '0' + links / 10;
    msg[len++] = '0' + links % 10;
    msg[len++] = '\n';
    write(2, msg, (size_t)len);
    return 1;     /* 故意非 0：KeepAlive 会反复执行，保证补正及时 */
}

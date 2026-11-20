/*
 * tpkg — TransputerOS Package Manager
 *
 * Manages .tpz packages (gzip'd tar with .tpz-info manifest).
 * Reads the archive directly and maintains an install database
 * in /var/lib/tpkg/<package>/{info,files}.
 *
 * Usage:
 *   tpkg install <file.tpz>     Install a package
 *   tpkg remove  <package>      Remove an installed package
 *   tpkg list                   List installed packages
 *   tpkg info    <package>      Show package details
 *   tpkg files   <package>      List files owned by a package
 */

#include "tpkg.h"
#include "tpz.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TPKG_DB      "/var/lib/tpkg"
#define MAX_LINE     256
#define MAX_PATH     (TPZ_PATH + 16)
#define INSTALL_LOCK TPKG_DB "/.install"
#define MAX_FILES    512
#define MAX_PACKAGES 64
#define MAX_DEPS     16
#define MAX_DEPNAME  32

/* The GUI shows the result in its own status area. */
#ifdef TPKG_EMBEDDED
#define REPORT(stream, ...) ((void)0)
#else
#define REPORT(stream, ...) fprintf(stream, __VA_ARGS__)
#endif

#ifdef __transputer__
/* The compact guest exposes the legacy mkdir slot. */
extern long __transputer_syscall(long, long, long, long, long, long, long);
static int tpkg_mkdir(const char *path, mode_t mode)
{
    long result = __transputer_syscall(1030, (long)path, mode, 0, 0, 0, 0);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return 0;
}
#define mkdir tpkg_mkdir
#endif

/* -----------------------------------------------------------------------
 * Utility: simple mkdir -p (create parent directories)
 * ----------------------------------------------------------------------- */
static void mkdirp(const char *path)
{
    static char tmp[MAX_PATH];
    char *p;
    int len;

    len = strlen(path);
    if (len >= MAX_PATH) return;
    strcpy(tmp, path);

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* -----------------------------------------------------------------------
 * Read a field from a .tpz-info style file
 * Returns pointer to static buffer, or "" if not found
 * ----------------------------------------------------------------------- */
static char field_buf[MAX_LINE];

static const char *read_field(const char *infopath, const char *key)
{
    FILE *f;
    char line[MAX_LINE];
    int klen = strlen(key);

    field_buf[0] = 0;
    f = fopen(infopath, "r");
    if (!f) return field_buf;

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;

        /* Match "Key: Value" */
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            const char *val = line + klen + 1;
            while (*val == ' ') val++;
            strncpy(field_buf, val, MAX_LINE - 1);
            field_buf[MAX_LINE - 1] = 0;
            break;
        }
    }
    fclose(f);
    return field_buf;
}

/* -----------------------------------------------------------------------
 * Dependency checking
 * ----------------------------------------------------------------------- */

/* Check if a package is installed */
static int is_installed(const char *pkgname)
{
    static char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s/info", TPKG_DB, pkgname);
    return access(path, R_OK) == 0;
}

/* Parse comma-separated dependency string into array.
 * Returns number of deps found. Trims whitespace. */
static int parse_deps(const char *depstr, char deps[][MAX_DEPNAME], int max)
{
    int n = 0;
    const char *p = depstr;

    while (*p && n < max) {
        int len = 0;
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Read until comma or end */
        while (*p && *p != ',' && len < MAX_DEPNAME - 1) {
            deps[n][len++] = *p++;
        }
        /* Trim trailing whitespace */
        while (len > 0 && (deps[n][len-1] == ' ' || deps[n][len-1] == '\t'))
            len--;
        deps[n][len] = 0;

        if (len > 0) n++;
        if (*p == ',') p++;
    }
    return n;
}

/* Check all dependencies. Returns 0 if satisfied, 1 if missing.
 * Prints missing deps to stderr. */
static int check_deps(const char *infopath)
{
    char deps[MAX_DEPS][MAX_DEPNAME];
    const char *depstr;
    int ndeps, i;
    int missing = 0;

    depstr = read_field(infopath, "Depends");
    if (!depstr[0]) return 0;  /* no deps */

    ndeps = parse_deps(depstr, deps, MAX_DEPS);

    for (i = 0; i < ndeps; i++) {
        if (!is_installed(deps[i])) {
            REPORT(stderr, "  MISSING dependency: %s\n", deps[i]);
            missing++;
        }
    }

    return missing;
}

static int package_name(const char *name)
{
    size_t len = strlen(name);
    return len && len < MAX_DEPNAME && name[0] != '.' &&
        strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-._+") == len;
}

static int payload_path(char *absolute, const char *relative)
{
    size_t len = strlen(relative);
    if (!len || !strcmp(relative, ".tpz-info") ||
        (!strncmp(relative, "var/lib/tpkg", 12) &&
         (relative[12] == '/' || !relative[12])) ||
        (len >= 9 && (!strcmp(relative + len - 9, ".tpkg-new") ||
                      !strcmp(relative + len - 9, ".tpkg-old"))))
        return -1;
    snprintf(absolute, MAX_PATH, "/%s", relative);
    return 0;
}

static int make_directory(const char *path)
{
    struct stat st;
    if (mkdir(path, 0755) == 0)
        return 0;
    return errno == EEXIST && lstat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

static int parents(char *path)
{
    char *slash = path + 1;
    while ((slash = strchr(slash, '/')) != NULL) {
        int rc;
        *slash = 0;
        rc = make_directory(path);
        *slash++ = '/';
        if (rc)
            return -1;
    }
    return 0;
}

/* Stage writes next to their destination so rename works across mounts. */
static FILE *stage_file(const char *path, unsigned mode)
{
    static char temp[MAX_PATH + 16];
    FILE *file;
    int fd;
    snprintf(temp, sizeof(temp), "%s.tpkg-new", path);
    fd = open(temp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (fd < 0)
        return NULL;
    file = fdopen(fd, "w+b");
    if (!file) {
        close(fd);
        unlinkat(AT_FDCWD, temp, 0);
    }
    return file;
}

static void discard_stage(const char *path)
{
    static char temp[MAX_PATH + 16];
    snprintf(temp, sizeof(temp), "%s.tpkg-new", path);
    unlinkat(AT_FDCWD, temp, 0);
}

/* A backup directory marks a file with no previous version. The backup
 * also prevents a later archive member from writing this path again. */
static void finish_file(const char *path, int keep)
{
    static char backup[MAX_PATH + 16];
    struct stat st;
    snprintf(backup, sizeof(backup), "%s.tpkg-old", path);
    if (lstat(backup, &st))
        return;
    if (S_ISDIR(st.st_mode)) {
        if (!keep)
            unlinkat(AT_FDCWD, path, 0);
        unlinkat(AT_FDCWD, backup, AT_REMOVEDIR);
    } else if (keep) {
        unlinkat(AT_FDCWD, backup, 0);
    } else {
        renameat(AT_FDCWD, backup, AT_FDCWD, path);
    }
}

static int publish_file(const char *path)
{
    static char temp[MAX_PATH + 16], backup[MAX_PATH + 16];
    struct stat st;
    snprintf(temp, sizeof(temp), "%s.tpkg-new", path);
    snprintf(backup, sizeof(backup), "%s.tpkg-old", path);
    if (lstat(backup, &st) == 0 || errno != ENOENT)
        return -1;
    if (lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || renameat(AT_FDCWD, path, AT_FDCWD, backup))
            return -1;
    } else if (errno != ENOENT || mkdir(backup, 0700)) {
        return -1;
    }
    if (renameat(AT_FDCWD, temp, AT_FDCWD, path)) {
        finish_file(path, 0);
        return -1;
    }
    return 0;
}

static int install_member(FILE *archive, struct tpz_member *member, FILE *files)
{
    static char path[MAX_PATH];
    FILE *out;
    int error;
    if (!member->path[0] && member->directory)
        return 0;
    if (payload_path(path, member->path) || parents(path))
        return -1;
    if (member->directory)
        return make_directory(path);
    out = stage_file(path, member->mode);
    if (!out)
        return -1;
    error = tpz_copy(archive, out, member->size);
    if (fclose(out))
        error = -1;
    if (error || publish_file(path)) {
        discard_stage(path);
        return -1;
    }
    if (fprintf(files, "%s\n", member->path) < 0 || fflush(files)) {
        finish_file(path, 0);
        return -1;
    }
    return 0;
}

static void finish_payload(FILE *files, int keep)
{
    static char path[MAX_PATH];
    char relative[TPZ_PATH + 1];
    rewind(files);
    while (fgets(relative, sizeof(relative), files)) {
        char *newline = strchr(relative, '\n');
        if (!newline)
            break;
        *newline = 0;
        if (!payload_path(path, relative))
            finish_file(path, keep);
    }
}

static int install_payload(FILE *archive, const char *info, const char *files_path)
{
    struct tpz_member member;
    int next, status = 1;
    FILE *files = stage_file(files_path, 0644);
    if (!files)
        return 1;
    while ((next = tpz_next(archive, &member)) > 0)
        if (install_member(archive, &member, files))
            break;
    if (next == 0 && !fflush(files) && !publish_file(info)) {
        if (!publish_file(files_path)) {
            finish_file(files_path, 1);
            status = 0;
        }
        finish_file(info, status == 0);
    }
    finish_payload(files, status == 0);
    fclose(files);
    discard_stage(files_path);
    return status;
}

static int cmd_install(const char *tpzfile)
{
    struct tpz_member member;
    static char info[MAX_PATH], files[MAX_PATH], temp_info[MAX_PATH + 16];
    char manifest[4096], name[MAX_DEPNAME];
    FILE *archive, *out;
    char *line;
    int status = 1, rc;

    archive = tpz_open(tpzfile);
    if (!archive) {
        REPORT(stderr, "tpkg: cannot unpack %s\n", tpzfile);
        return 1;
    }
    if (tpz_next(archive, &member) != 1 || member.directory ||
        strcmp(member.path, ".tpz-info") || member.size >= sizeof(manifest) ||
        fread(manifest, 1, member.size, archive) != member.size ||
        fseek(archive, (-member.size) & 511, SEEK_CUR))
        goto done;
    manifest[member.size] = 0;
    line = manifest;
    while (strncmp(line, "Package:", 8) != 0) {
        line = strchr(line, '\n');
        if (!line)
            goto done;
        line++;
    }
    line += 8;
    line += strspn(line, " \t");
    if (strcspn(line, "\r\n") >= sizeof(name))
        goto done;
    snprintf(name, sizeof(name), "%.*s", (int)strcspn(line, "\r\n"), line);
    if (!package_name(name))
        goto done;

    snprintf(info, sizeof(info), "%s/%s/info", TPKG_DB, name);
    snprintf(files, sizeof(files), "%s/%s/files", TPKG_DB, name);
    snprintf(temp_info, sizeof(temp_info), "%s.tpkg-new", info);
    if (parents(info) || !(out = stage_file(info, 0644)))
        goto done;
    rc = fwrite(manifest, 1, member.size, out) != member.size;
    if (fclose(out))
        rc = 1;
    if (!rc && !check_deps(temp_info)) {
        REPORT(stdout, "Installing %s %s ...\n", name, read_field(temp_info, "Version"));
        status = install_payload(archive, info, files);
    }
    discard_stage(info);
    if (!status)
        REPORT(stdout, "  %s installed\n", name);
done:
    if (status)
        REPORT(stderr, "tpkg: installation of %s failed\n", tpzfile);
    fclose(archive);
    return status;
}

static int cmd_remove(const char *pkgname)
{
    static char info[MAX_PATH], files[MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
    char line[TPZ_PATH + 4], normalized[TPZ_PATH];
    FILE *f;
    int count = 0, failed = 0;

    if (!package_name(pkgname))
        return 1;
    snprintf(dir, sizeof(dir), "%s/%s", TPKG_DB, pkgname);
    snprintf(info, sizeof(info), "%s/%s/info", TPKG_DB, pkgname);
    snprintf(files, sizeof(files), "%s/%s/files", TPKG_DB, pkgname);
    f = fopen(files, "r");
    if (!f || access(info, R_OK) != 0) {
        if (f)
            fclose(f);
        REPORT(stderr, "tpkg: package '%s' is not installed\n", pkgname);
        return 1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *end = strpbrk(line, "\r\n");
        if (!end) {
            failed = 1;
            break;
        }
        *end = 0;
        if (!line[0] || !strcmp(line, "./") || !strcmp(line, "."))
            continue;
        if (tpz_relative_path(normalized, line) || payload_path(path, normalized)) {
            failed = 1;
            break;
        }
        if (unlinkat(AT_FDCWD, path, 0) == 0)
            count++;
        else if (errno != ENOENT && errno != EISDIR)
            failed = 1;
    }
    failed |= ferror(f);
    fclose(f);
    if (failed || unlinkat(AT_FDCWD, info, 0) || unlinkat(AT_FDCWD, files, 0) ||
        unlinkat(AT_FDCWD, dir, AT_REMOVEDIR)) {
        REPORT(stderr, "tpkg: removal of %s failed\n", pkgname);
        return 1;
    }
    REPORT(stdout, "  %s removed (%d files deleted)\n", pkgname, count);
    return 0;
}

static int with_install_lock(int (*operation)(const char *), const char *arg)
{
    int rc;
    mkdirp(TPKG_DB);
    if (mkdir(INSTALL_LOCK, 0700)) {
        REPORT(stderr, "tpkg: cannot lock the package database\n");
        return 1;
    }
    rc = operation(arg);
    if (unlinkat(AT_FDCWD, INSTALL_LOCK, AT_REMOVEDIR))
        return 1;
    return rc;
}

int tpkg_install(const char *archive)
{
    return with_install_lock(cmd_install, archive);
}

int tpkg_remove(const char *package)
{
    return with_install_lock(cmd_remove, package);
}

#ifndef TPKG_EMBEDDED
/* -----------------------------------------------------------------------
 * tpkg list
 * ----------------------------------------------------------------------- */
static int cmd_list(void)
{
    char cmd[MAX_PATH];
    char line[MAX_PATH];
    char infopath[MAX_PATH];
    FILE *f;

    /* List directories in TPKG_DB */
    snprintf(cmd, sizeof(cmd), "ls %s 2>/dev/null", TPKG_DB);
    f = popen(cmd, "r");
    if (!f) {
        printf("No packages installed.\n");
        return 0;
    }

    printf("%-20s %-12s %s\n", "PACKAGE", "VERSION", "DESCRIPTION");
    printf("%-20s %-12s %s\n", "-------", "-------", "-----------");

    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = 0;
        if (len == 0) continue;

        snprintf(infopath, sizeof(infopath), "%s/%s/info", TPKG_DB, line);
        if (access(infopath, R_OK) != 0) continue;

        {
            const char *ver = read_field(infopath, "Version");
            char verbuf[16];
            strncpy(verbuf, ver, 15);
            verbuf[15] = 0;

            const char *desc = read_field(infopath, "Description");
            printf("%-20s %-12s %s\n", line, verbuf, desc);
        }
    }
    pclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * tpkg info <package>
 * ----------------------------------------------------------------------- */
static int cmd_info(const char *pkgname)
{
    char infopath[MAX_PATH];
    char line[MAX_LINE];
    FILE *f;

    snprintf(infopath, sizeof(infopath), "%s/%s/info", TPKG_DB, pkgname);
    f = fopen(infopath, "r");
    if (!f) {
        fprintf(stderr, "tpkg: package '%s' is not installed\n", pkgname);
        return 1;
    }

    while (fgets(line, sizeof(line), f))
        fputs(line, stdout);

    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * tpkg files <package>
 * ----------------------------------------------------------------------- */
static int cmd_files(const char *pkgname)
{
    char filespath[MAX_PATH];
    char line[MAX_PATH];
    FILE *f;

    snprintf(filespath, sizeof(filespath), "%s/%s/files", TPKG_DB, pkgname);
    f = fopen(filespath, "r");
    if (!f) {
        fprintf(stderr, "tpkg: package '%s' is not installed\n", pkgname);
        return 1;
    }

    while (fgets(line, sizeof(line), f))
        fputs(line, stdout);

    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */
static void usage(void)
{
    fprintf(stderr,
        "tpkg — TransputerOS Package Manager\n"
        "\n"
        "Usage:\n"
        "  tpkg install  <file.tpz>  Install a package (checks deps)\n"
        "  tpkg remove   <package>   Remove a package\n"
        "  tpkg list                 List installed packages\n"
        "  tpkg info     <package>   Show package details\n"
        "  tpkg files    <package>   List files in a package\n"
        "  tpkg depends  <package>   Show dependencies\n"
        "  tpkg rdepends <package>   Show reverse dependencies\n"
    );
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    /* Ensure database directory exists */
    mkdirp(TPKG_DB);

    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg install: missing filename\n"); return 1; }
        return tpkg_install(argv[2]);
    }
    if (strcmp(argv[1], "depends") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg depends: missing package name\n"); return 1; }
        {
            char infopath[MAX_PATH];
            const char *depstr;
            snprintf(infopath, sizeof(infopath), "%s/%s/info", TPKG_DB, argv[2]);
            if (access(infopath, R_OK) != 0) {
                fprintf(stderr, "tpkg: package '%s' is not installed\n", argv[2]);
                return 1;
            }
            depstr = read_field(infopath, "Depends");
            if (depstr[0])
                printf("%s\n", depstr);
            else
                printf("(none)\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "rdepends") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg rdepends: missing package name\n"); return 1; }
        {
            char cmd[MAX_PATH];
            char line[MAX_PATH];
            char infopath[MAX_PATH];
            FILE *f;
            int found = 0;
            snprintf(cmd, sizeof(cmd), "ls %s 2>/dev/null", TPKG_DB);
            f = popen(cmd, "r");
            if (f) {
                while (fgets(line, sizeof(line), f)) {
                    int len = strlen(line);
                    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                        line[--len] = 0;
                    snprintf(infopath, sizeof(infopath), "%s/%s/info", TPKG_DB, line);
                    {
                        const char *depstr = read_field(infopath, "Depends");
                        if (depstr[0]) {
                            char deps[MAX_DEPS][MAX_DEPNAME];
                            int ndeps = parse_deps(depstr, deps, MAX_DEPS);
                            int i;
                            for (i = 0; i < ndeps; i++) {
                                if (strcmp(deps[i], argv[2]) == 0) {
                                    printf("%s\n", line);
                                    found++;
                                    break;
                                }
                            }
                        }
                    }
                }
                pclose(f);
            }
            if (!found) printf("(none)\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg remove: missing package name\n"); return 1; }
        return tpkg_remove(argv[2]);
    }
    if (strcmp(argv[1], "list") == 0) {
        return cmd_list();
    }
    if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg info: missing package name\n"); return 1; }
        return cmd_info(argv[2]);
    }
    if (strcmp(argv[1], "files") == 0) {
        if (argc < 3) { fprintf(stderr, "tpkg files: missing package name\n"); return 1; }
        return cmd_files(argv[2]);
    }

    fprintf(stderr, "tpkg: unknown command '%s'\n", argv[1]);
    usage();
    return 1;
}
#endif

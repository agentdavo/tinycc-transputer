/*
 * tpkg — TransputerOS Package Manager
 *
 * Manages .tpz packages (gzip'd tar with .tpz-info manifest).
 * Uses BusyBox tar/gzip for extraction, maintains an install database
 * in /var/lib/tpkg/<package>/{info,files}.
 *
 * Usage:
 *   tpkg install <file.tpz>     Install a package
 *   tpkg remove  <package>      Remove an installed package
 *   tpkg list                   List installed packages
 *   tpkg info    <package>      Show package details
 *   tpkg files   <package>      List files owned by a package
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TPKG_DB      "/var/lib/tpkg"
#define MAX_LINE     256
#define MAX_PATH     256
#define MAX_FILES    512
#define MAX_PACKAGES 64
#define MAX_DEPS     16
#define MAX_DEPNAME  32

/* -----------------------------------------------------------------------
 * Utility: simple mkdir -p (create parent directories)
 * ----------------------------------------------------------------------- */
static void mkdirp(const char *path)
{
    char tmp[MAX_PATH];
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
 * Extract .tpz-info from a .tpz archive to a buffer file
 * Returns 0 on success
 * ----------------------------------------------------------------------- */
static int extract_info(const char *tpzfile, const char *outpath)
{
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd),
        "tar xzf %s -O .tpz-info > %s 2>/dev/null", tpzfile, outpath);
    return system(cmd);
}

/* -----------------------------------------------------------------------
 * Dependency checking
 * ----------------------------------------------------------------------- */

/* Check if a package is installed */
static int is_installed(const char *pkgname)
{
    char path[MAX_PATH];
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
            fprintf(stderr, "  MISSING dependency: %s\n", deps[i]);
            missing++;
        }
    }

    return missing;
}

/* -----------------------------------------------------------------------
 * tpkg install <file.tpz>
 * ----------------------------------------------------------------------- */
static int cmd_install(const char *tpzfile)
{
    char tmpinfo[MAX_PATH];
    char dbdir[MAX_PATH];
    char infopath[MAX_PATH];
    char filespath[MAX_PATH];
    char cmd[MAX_PATH * 2];
    const char *pkgname;
    const char *version;
    FILE *f;

    /* Check file exists */
    if (access(tpzfile, R_OK) != 0) {
        fprintf(stderr, "tpkg: cannot read %s\n", tpzfile);
        return 1;
    }

    /* Extract manifest to temp file */
    strcpy(tmpinfo, "/tmp/.tpkg-info-tmp");
    if (extract_info(tpzfile, tmpinfo) != 0) {
        fprintf(stderr, "tpkg: failed to read .tpz-info from %s\n", tpzfile);
        return 1;
    }

    pkgname = read_field(tmpinfo, "Package");
    if (!pkgname[0]) {
        fprintf(stderr, "tpkg: no Package field in .tpz-info\n");
        unlink(tmpinfo);
        return 1;
    }

    version = read_field(tmpinfo, "Version");

    printf("Installing %s %s ...\n", pkgname, version);

    /* Check dependencies */
    {
        int nmissing = check_deps(tmpinfo);
        if (nmissing > 0) {
            fprintf(stderr, "tpkg: %d missing dependenc%s — install them first\n",
                    nmissing, nmissing == 1 ? "y" : "ies");
            unlink(tmpinfo);
            return 1;
        }
    }

    /* Create database directory */
    snprintf(dbdir, sizeof(dbdir), "%s/%s", TPKG_DB, pkgname);
    mkdirp(dbdir);

    /* Extract package to root */
    snprintf(cmd, sizeof(cmd), "tar xzf %s -C / 2>/dev/null", tpzfile);
    if (system(cmd) != 0) {
        fprintf(stderr, "tpkg: extraction failed\n");
        return 1;
    }

    /* Save manifest to database */
    snprintf(infopath, sizeof(infopath), "%s/info", dbdir);
    snprintf(cmd, sizeof(cmd), "cp %s %s", tmpinfo, infopath);
    system(cmd);

    /* Record file list */
    snprintf(filespath, sizeof(filespath), "%s/files", dbdir);
    snprintf(cmd, sizeof(cmd),
        "tar tzf %s 2>/dev/null | grep -v '^\\./\\?$' | grep -v '^\\.tpz-info$' > %s",
        tpzfile, filespath);
    system(cmd);

    unlink(tmpinfo);

    /* Count files */
    {
        int count = 0;
        char line[MAX_PATH];
        f = fopen(filespath, "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) count++;
            fclose(f);
        }
        printf("  %s %s installed (%d files)\n", pkgname, version, count);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * tpkg remove <package>
 * ----------------------------------------------------------------------- */
static int cmd_remove(const char *pkgname)
{
    char dbdir[MAX_PATH];
    char filespath[MAX_PATH];
    char infopath[MAX_PATH];
    char filepath[MAX_PATH];
    char line[MAX_PATH];
    FILE *f;
    int count = 0;
    const char *version;

    snprintf(dbdir, sizeof(dbdir), "%s/%s", TPKG_DB, pkgname);
    snprintf(infopath, sizeof(infopath), "%s/info", dbdir);
    snprintf(filespath, sizeof(filespath), "%s/files", dbdir);

    if (access(infopath, R_OK) != 0) {
        fprintf(stderr, "tpkg: package '%s' is not installed\n", pkgname);
        return 1;
    }

    version = read_field(infopath, "Version");
    printf("Removing %s %s ...\n", pkgname, version);

    /* Delete installed files (in reverse order to remove files before dirs) */
    f = fopen(filespath, "r");
    if (f) {
        /* Read all paths, delete files first */
        while (fgets(line, sizeof(line), f)) {
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = 0;
            if (len == 0) continue;

            /* Convert ./path to /path */
            if (line[0] == '.' && line[1] == '/')
                snprintf(filepath, sizeof(filepath), "/%s", line + 2);
            else
                snprintf(filepath, sizeof(filepath), "/%s", line);

            if (unlink(filepath) == 0)
                count++;
        }
        fclose(f);
    }

    /* Remove database entry */
    unlink(infopath);
    unlink(filespath);
    rmdir(dbdir);

    printf("  %s removed (%d files deleted)\n", pkgname, count);
    return 0;
}

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
        return cmd_install(argv[2]);
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
        return cmd_remove(argv[2]);
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

int printf(const char *, ...);
struct con { unsigned int flags; const char *name; short index; };
static struct con boot_con = { 0x0004u, "earlycon", 15 };
static struct con tty_con = { 0x0000u, "ttyS", 0 };
#define CON_NBCON 0x0001u
#define CON_BOOT  0x0004u
static void show(struct con *c, const char *what) {
    printf("%s%sconsole [%s%d] %s\n",
           (c->flags & CON_NBCON) ? "" : "legacy ",
           (c->flags & CON_BOOT) ? "boot" : "",
           c->name, c->index, what);
}
int main(void) {
    show(&boot_con, "enabled");
    show(&tty_con, "enabled");
    show(&boot_con, "disabled");
    return 0;
}

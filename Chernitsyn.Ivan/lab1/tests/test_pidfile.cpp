#include <stdint.h>
#include <criterion/criterion.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pidfile.h"

static char path[64];

static void setup(void)
{
    snprintf(path, sizeof(path), "/tmp/reminder_pid_test_%ld", (long)getpid());
    unlink(path);
}

static void teardown(void)
{
    unlink(path);
}

TestSuite(pidfile, .init = setup, .fini = teardown);

Test(pidfile, write_read)
{
    cr_assert_eq(pidfile_write(path, 12345), 0);
    cr_assert_eq(pidfile_read(path), 12345);
}

Test(pidfile, read_missing)
{
    cr_assert_eq(pidfile_read(path), 0);
}

Test(pidfile, read_garbage)
{
    FILE *f = fopen(path, "w");
    fputs("not a pid\n", f);
    fclose(f);
    cr_assert_eq(pidfile_read(path), 0);
}

Test(pidfile, remove_only_own)
{
    pidfile_write(path, 1);
    pidfile_remove(path);
    cr_assert_eq(access(path, F_OK), 0);

    pidfile_write(path, getpid());
    pidfile_remove(path);
    cr_assert_neq(access(path, F_OK), 0);
}

Test(pidfile, alive)
{
    cr_assert(process_alive(getpid()));
    cr_assert(process_alive(1));
    cr_assert_not(process_alive(0));
    cr_assert_not(process_alive(-5));
}

Test(pidfile, stop_nothing)
{
    cr_assert_eq(stop_running(path), 0);

    pidfile_write(path, getpid());
    cr_assert_eq(stop_running(path), 0);
}

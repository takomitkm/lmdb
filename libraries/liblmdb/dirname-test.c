/* dirname-test.c - unit test for mdb_dirname(), the static helper added by
 * the directory-flush patch.  It is compiled with the arm's mdb.c #included,
 * so it can call the static function directly and needs no filesystem access
 * (which is how the drive-root and UNC rows are covered without a server or
 * administrator rights).
 *
 * usage: dirname-test        (exit 0 = all rows match)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdb.c"

static int failures;

static void check(const char *path, const char *want)
{
	char *got = mdb_dirname(path);
	if (!got) {
		printf("FAIL %-28s -> NULL (want \"%s\")\n", path, want);
		failures++;
		return;
	}
	if (strcmp(got, want) != 0) {
		printf("FAIL %-28s -> \"%s\" (want \"%s\")\n", path, got, want);
		failures++;
	} else {
		printf("ok   %-28s -> \"%s\"\n", path, got);
	}
	free(got);
}

int main(void)
{
#ifdef _WIN32
	/* the table from the review, Windows half */
	check("db.mdb", ".");
	check("dir\\db.mdb", "dir");
	check("dir/db.mdb", "dir");
	check("C:db.mdb", "C:");
	check("C:\\db.mdb", "C:\\");
	check("C:\\dir\\db.mdb", "C:\\dir");
	check("C:/dir/db.mdb", "C:/dir");
	check("\\\\server\\share\\db.mdb", "\\\\server\\share");
	check("\\\\?\\C:\\db.mdb", "\\\\?\\C:\\");
	check("\\\\?\\C:\\dir\\db.mdb", "\\\\?\\C:\\dir");
	check("\\\\.\\C:\\db.mdb", "\\\\.\\C:\\");
	check("dir\\sub\\db.mdb", "dir\\sub");
#else
	/* POSIX: '\\' is an ordinary file-name character, not a separator */
	check("db.mdb", ".");
	check("dir/db.mdb", "dir");
	check("/db.mdb", "/");
	check("//db.mdb", "//");
	check("///db.mdb", "/");
	check("////db.mdb", "/");
	check("///dir/db.mdb", "/dir");
	check("//dir/db.mdb", "//dir");
	check("dir\\sub\\db.mdb", ".");
	check("foo\\bar.mdb", ".");
	check("dir/sub/db.mdb", "dir/sub");
	check("dir/db.mdb/", "dir");
#endif
	printf(failures ? "DIRNAME-TEST FAILED (%d)\n" : "DIRNAME-TEST OK\n", failures);
	return failures ? 1 : 0;
}

#ifndef __inihck_h__
#define __inihck_h__

#include <stddef.h>
#include <stdbool.h>

#if __GNUC__
#  define INI_NONULL __attribute__((nonnull))
#  define INI_NONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#  define INI_NONULL
#  define INI_NONULLV
#endif

struct ini;
struct ini_data;

INI_NONULL typedef void (*ini_throw_cb)(struct ini *ini, size_t line_num, size_t position, const char *line, const char *message);

struct ini {
   struct ini_data *data;
   ini_throw_cb throw; // set to ini_throw_cb function to catch parsing errors
   char delim;
};

struct ini_options {
   bool escaping;
   bool quoted_strings;
   bool empty_values;
   bool empty_keys;
};

struct ini_value {
   const char *data;
   size_t size;
};

struct ini_iterator {
   const char *path;
};

#define ini_for_each_call(ini, function, ...) \
{ struct ini_value v; for (struct ini_iterator _I = { NULL }; ini_iter(ini, &_I, &v);) function(&v, ##__VA_ARGS__); }

#define ini_for_each(ini, v) \
   for (struct ini_iterator _I = { NULL }; ini_iter(ini, &_I, v);)

INI_NONULLV(1) bool ini(struct ini *ini, char delim, size_t size, ini_throw_cb cb);
void ini_release(struct ini *ini);
INI_NONULL void ini_flush(struct ini *ini);
INI_NONULLV(1,2) bool ini_parse(struct ini *ini, const char *path, const struct ini_options *options);
INI_NONULLV(1,2) bool ini_get(struct ini *ini, const char *path, struct ini_value *out_value);
INI_NONULL bool ini_iter(struct ini *ini, struct ini_iterator *iterator, struct ini_value *out_value);
INI_NONULL void ini_print(struct ini *ini);

#endif /* __inihck_h__ */

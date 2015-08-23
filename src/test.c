#include <inihck/inihck.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#undef NDEBUG
#include <assert.h>

#if FUZZ
#  undef assert
#  undef strncmp
#  define assert(x) (void)x
#  define strncmp(x, y, z) false
#endif

static void
throw(struct ini *ini, size_t line_num, size_t position, const char *line, const char *message)
{
   (void)ini;
   printf("[%zu, %zu]: %s\n", line_num, position, message);
   printf("%s\n%*c\n", line, (uint32_t)position, '^');
}

int main(void)
{
   struct ini inif;
   assert(ini(&inif, '.', 256, NULL));

   {
      assert(!ini_parse(&inif, "test.ini", NULL));
      ini_flush(&inif);
   }

   {
      struct ini_options options = { .escaping = true, .quoted_strings = false, .empty_values = false, .empty_keys = false };
      assert(!ini_parse(&inif, "test.ini", &options));
      ini_flush(&inif);
   }

   {
      struct ini_options options = { .escaping = true, .quoted_strings = true, .empty_values = false, .empty_keys = false };
      assert(!ini_parse(&inif, "test.ini", &options));
      ini_flush(&inif);
   }

   {
      struct ini_options options = { .escaping = true, .quoted_strings = true, .empty_values = true, .empty_keys = false };
      assert(!ini_parse(&inif, "test.ini", &options));
      ini_flush(&inif);
   }

   ini_release(&inif);

   assert(ini(&inif, '.', 256, throw));

   {
      struct ini_options options = { .escaping = true, .quoted_strings = true, .empty_values = true, .empty_keys = true };
      assert(ini_parse(&inif, "test.ini", &options));
   }

   struct ini_value value;
   assert(ini_get(&inif, "foo.bar", &value));
   assert(!strncmp(value.data, "foo UTF16: 🏩 UTF32: 🏩newline\nyeah\r\n\t\b\\0 ← null terminator", value.size));
   assert(ini_get(&inif, "foo.empty", &value));
   assert(!strncmp(value.data, "", value.size));
   assert(ini_get(&inif, "foo.empty2", &value));
   assert(!strncmp(value.data, "", value.size));
   assert(ini_get(&inif, "foo.bar2", &value));
   assert(!strncmp(value.data, "asd", value.size));
   assert(ini_get(&inif, ".foo", &value));
   assert(!strncmp(value.data, "bar", value.size));
   assert(ini_get(&inif, "valid[.valid", &value));
   assert(!strncmp(value.data, "hah", value.size));
   assert(ini_get(&inif, "valid[.valid2", &value));
   assert(!strncmp(value.data, "long string\nthat \"goes on\"", value.size));
   assert(!ini_get(&inif, "foo.asd", NULL));
   assert(!ini_get(&inif, ".asd", NULL));
   assert(!ini_get(&inif, "foo.foo", NULL));

   ini_print(&inif);
   ini_release(&inif);
   return EXIT_SUCCESS;
}

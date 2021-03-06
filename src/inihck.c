#include <inihck/inihck.h>
#include <chck/lut/lut.h>
#include <chck/pool/pool.h>
#include <chck/string/string.h>
#include <chck/unicode/unicode.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>

struct ini_data {
   struct chck_hash_table table;
   struct chck_hash_table_iterator iterator;
};

struct state {
   struct ini_options options;
   struct chck_string key; // current key
   struct chck_string section; // current section
   const char *cursor; // current char
   const char *line_start; // where line started
   const char *buffer;
   size_t line, size;
   uint16_t utf16_hi;
};

static void
throw_message(struct ini *ini, const struct state *state, const char *message)
{
   assert(ini && state && message);

   if (!ini->throw)
      return;

   char line[128];
   const size_t avail = state->size - (state->line_start - state->buffer);
   const size_t len = (avail >= sizeof(line) ? sizeof(line) : avail);
   strncpy(line, state->line_start, len);
   line[len] = 0;

   const size_t nl = strcspn(line, "\n\r\v\f");
   const size_t end = (nl < sizeof(line) ? nl : sizeof(line));
   line[end] = 0;

   ini->throw(ini, state->line, (size_t)(state->cursor - state->line_start + 1), line, message);
}

static void
throw(struct ini *ini, const struct state *state, const char *fmt, ...)
{
   assert(ini && state && fmt);
   char message[128];
   va_list args;
   va_start(args, fmt);
   vsnprintf(message, sizeof(message) - 1, fmt, args);
   va_end(args);
   throw_message(ini, state, message);
}

static bool
is_eol(char chr)
{
   /** not including LS, PS or NEL */
   return (chr == '\n' || chr == '\r' || chr == '\v' || chr == '\f');
}

static bool
is_eol_or_space(char chr)
{
   return (is_eol(chr) || isspace(chr));
}

static bool
state_end(const struct state *state)
{
   return ((size_t)(state->cursor - state->buffer) >= state->size);
}

static char
advance(struct state *state, bool skip_whitespace)
{
   assert(state);

   if (state_end(state))
      return 0;

   do {
      if (++state->cursor && !state_end(state) && is_eol(*state->cursor)) {
         state->cursor += (*(state->cursor - 1) == '\r' && *state->cursor == '\n');
         ++state->line;
         state->line_start = state->cursor + 1;
      }
   } while (!state_end(state) && (is_eol(*state->cursor) || (skip_whitespace && isspace(*state->cursor)) || !*state->cursor));

   return (state_end(state) ? 0 : *state->cursor);
}

static bool
is_hex(char chr)
{
   return (chr >= '0' && chr <= '9') || (chr >= 'A' && chr <= 'F') || (chr >= 'a' && chr <= 'f');
}

static uint32_t
decode_hex(struct state *state, uint8_t len)
{
   assert(state && len < 9);
   char hex[9] = {0}, chr;
   for (uint8_t i = 0; i < len && (chr = advance(state, false)) && is_hex(chr); ++i) hex[i] = chr;
   return strtoul(hex, NULL, 16);
}

static bool
decode_u8(struct ini *ini, struct state *state, uint32_t dec, struct chck_iter_pool *pool)
{
   assert(ini && state && pool);

   if (!dec) {
      throw(ini, state, "Invalid \\U escape");
      return false;
   }

   char u8[4];
   const uint8_t len = chck_utf32_encode(dec, u8);
   assert(len <= sizeof(u8));

   for (uint8_t i = 0; i < len; ++i)
      if (!chck_iter_pool_push_back(pool, u8 + i))
         return false;

   return true;
}

static bool
decode_u4(struct ini *ini, struct state *state, uint32_t dec, struct chck_iter_pool *pool)
{
   assert(ini && state && pool);

   if (!dec) {
      throw(ini, state, "Invalid \\u escape");
      return false;
   }

   char u8[4] = {0};
   enum chck_utf16_error error;
   const uint8_t len = chck_utf16_encode(dec, u8, &state->utf16_hi, &error);
   assert(len <= sizeof(u8));

   switch (error) {
      case CHCK_UTF16_OK:
         break;

      case CHCK_UTF16_UNEXPECTED_LOW:
         throw(ini, state, "Expected low UTF16 surrogate after high surrogate");
         return false;

      case CHCK_UTF16_UNEXPECTED_HIGH:
         throw(ini, state, "Expected high UTF16 surrogate before low surrogate");
         return false;
   }

   for (uint8_t i = 0; i < len; ++i)
      if (!chck_iter_pool_push_back(pool, u8 + i))
         return false;

   return true;
}

static bool
decode_escaped(struct ini *ini, struct state *state, struct chck_iter_pool *pool)
{
   assert(state && pool);

   if (!state->options.escaping || *state->cursor != '\\')
      return chck_iter_pool_push_back(pool, state->cursor);

   const char chr = advance(state, false);
   switch (*state->cursor) {
      case '\"': return chck_iter_pool_push_back(pool, "\"");
      case '0': return chck_iter_pool_push_back(pool, "\\") && chck_iter_pool_push_back(pool, "0");
      case 'b': return chck_iter_pool_push_back(pool, "\b");
      case 't': return chck_iter_pool_push_back(pool, "\t");
      case 'r': return chck_iter_pool_push_back(pool, "\r");
      case 'n': return chck_iter_pool_push_back(pool, "\n");
      case 'u': return decode_u4(ini, state, decode_hex(state, 4), pool);
      case 'U': return decode_u8(ini, state, decode_hex(state, 8), pool);
      default: return chck_iter_pool_push_back(pool, (char[]){ chr });
   }

   return false;
}

static bool
escape_eol(struct state *state, size_t *line)
{
   assert(state && line);

   if (!state->options.escaping)
      return false;

   if (*state->cursor != '\\' || !is_eol(*(state->cursor + 1)))
      return false;

   advance(state, true);
   *line = state->line;
   --state->cursor;
   return true;
}

static bool
set_value(struct ini *ini, const struct state *before, struct state *state, struct chck_iter_pool *pool)
{
   assert(ini && state);

   struct chck_string path = {0};
   const char *section = (chck_string_is_empty(&state->section) ? "" : state->section.data);
   if (!chck_string_set_format(&path, "%.*s%c%.*s", (int)state->section.size, section, ini->delim, (int)state->key.size, state->key.data)) {
      throw(ini, before, "Could not set key '%s.%s' (out of memory?)", section, state->key.data);
      return false;
   }

   if (chck_hash_table_str_get(&ini->data->table, path.data, path.size)) {
      throw(ini, before, "Key '%s' is already set", path.data);
      goto error0;
   }

   struct chck_string value = {0};

   if (pool) {
      // make sure value is null terminated
      if (pool->items.count > 0 && *(char*)chck_iter_pool_get_last(pool) != 0)
         chck_iter_pool_push_back(pool, "");

      // steal ownership from pool
      chck_string_set_cstr_with_length(&value, (void*)pool->items.buffer, pool->items.count, false);
      value.is_heap = true;
      pool->items.buffer = NULL;
   }

   if (!chck_hash_table_str_set(&ini->data->table, path.data, path.size, &value))
      goto error1;

   chck_string_release(&path);
   return true;

error1:
   chck_string_release(&value);
error0:
   chck_string_release(&path);
   return false;
}

static bool
parse_value(struct ini *ini, struct state *state)
{
   assert(ini && state);
   assert(*state->cursor == '=');

   struct chck_iter_pool pool;
   chck_iter_pool(&pool, 32, 0, sizeof(char));

   struct state before = *state;
   size_t line = state->line;
   bool started = false, is_quoted = false;
   while (advance(state, false)) {
      if (is_quoted && *state->cursor == '"') {
         break;
      } else if (state->line != line) {
         if (is_quoted) {
            chck_iter_pool_push_back(&pool, "\n");
            line = state->line;
         } else {
            break;
         }
      }

      if (escape_eol(state, &line) || (!started && isspace(*state->cursor)))
         continue;

      if (!started && *state->cursor == '"') {
         is_quoted = true;
         continue;
      }

      decode_escaped(ini, state, &pool);
      started = true;
   }

   if (is_quoted) {
      assert(state_end(state) || *state->cursor == '"');

      if (state_end(state) || *state->cursor != '"') {
         throw(ini, &before, "Unterminated quoted string");
         goto error0;
      }

      // skip ending "
      ++state->cursor;
   } else {
      assert(state_end(state) || state->line != line);
   }

   if (!started) {
      if (!state->options.empty_values) {
         throw(ini, &before, "Value should not be empty");
         goto error0;
      }

      goto out;
   }

out: ; // stupid C standard
   const bool ret = set_value(ini, &before, state, &pool);
   chck_iter_pool_release(&pool);
   return ret;

error0:
   chck_iter_pool_release(&pool);
   return false;
}

static bool
parse_key(struct ini *ini, struct state *state)
{
   assert(ini && state);
   assert(*state->cursor != '[' && *state->cursor != '#' && *state->cursor != ';');

   struct state before = *state;
   bool invalid_characters = false;
   bool has_whitespace = false;
   const char *start = state->cursor, *last = state->cursor, *end = NULL;
   while (advance(state, has_whitespace) && *state->cursor != '=' && state->line == before.line) {
      if (*state->cursor == ini->delim) {
         invalid_characters = true;
      } else if (isspace(*state->cursor)) {
         has_whitespace = (end ? true : false);
         end = state->cursor;
      } else {
         last = (end ? end : state->cursor);
      }
   }

   if (state_end(state))
      return false;

   // valueless key in other words
   bool is_empty_key = false;

   if (*state->cursor != '=') {
      if (!state->options.empty_keys) {
         throw(ini, &before, "Key does not end up with '='");
         return false;
      } else if (!has_whitespace) {
         has_whitespace = is_eol_or_space(*last);
      }

      is_empty_key = true;
   }

   if (!is_empty_key && state->line != before.line) {
      throw(ini, &before, "Key contains newline");
      return false;
   }

   if (has_whitespace) {
      throw(ini, &before, "Key contains whitespace");
      return false;
   }

   if (invalid_characters) {
      throw(ini, state, "Key contains invalid characters [%c]", ini->delim);
      return false;
   }

   if (last - start > INT_MAX) {
      throw(ini, state, "Key name too long");
      return false;
   }

   if (last >= start)
      chck_string_set_cstr_with_length(&state->key, start, last + 1 - start, false);

   if (chck_string_is_empty(&state->key)) {
      throw(ini, state, "Key is empty");
      return false;
   }

   return (is_empty_key ? set_value(ini, &before, state, NULL) : parse_value(ini, state));
}

static bool
parse_section(struct ini *ini, struct state *state)
{
   assert(ini && state);
   assert(*state->cursor == '[');

   struct state before = *state;
   bool has_whitespace = false;
   const char *start = state->cursor + 1;
   while (advance(state, has_whitespace) && *state->cursor != ']' && state->line == before.line) {
      if (isspace(*state->cursor))
         has_whitespace = true;
   }

   if (state_end(state))
      return false;

   if (*state->cursor != ']') {
      throw(ini, &before, "Section does not end up with ']'");
      return false;
   }

   // skip ending ] so we do not try to parse it
   ++state->cursor;

   if (state->line != before.line) {
      throw(ini, &before, "Section contains newline");
      return false;
   }

   if (has_whitespace) {
      throw(ini, state, "Section contains whitespace");
      return false;
   }

   if (state->cursor - start > INT_MAX) {
      throw(ini, state, "Section name too long");
      return false;
   }

   if (state->cursor > start)
      chck_string_set_cstr_with_length(&state->section, start, state->cursor - 1 - start, false);

   if (chck_string_is_empty(&state->section)) {
      throw(ini, state, "Section is empty");
      return false;
   }

   return true;
}

static bool
parse_comment(struct ini *ini, struct state *state)
{
   (void)ini;
   assert(ini && state);
   assert(*state->cursor == '#' || *state->cursor == ';');
   size_t line = state->line;
   while (advance(state, true) && state->line == line) escape_eol(state, &line);
   assert(state_end(state) || state->line != line);
   return true;
}

static char
next(struct state *state)
{
   if (state_end(state))
      return 0;

   return (!is_eol_or_space(*state->cursor) ? *state->cursor : advance(state, true));
}

static bool
parse(struct ini *ini, struct state *state)
{
   assert(ini && state);

   struct {
      char chr;
      bool (*parse)(struct ini*, struct state*);
   } map[] = {
      { '[', parse_section },
      { ';', parse_comment },
      { '#', parse_comment },
      { 0, parse_key },
   };

   char chr;
   bool valid = true;
   while ((chr = next(state))) {
      for (uint32_t i = 0;; ++i) {
         if (map[i].chr && map[i].chr != chr)
            continue;

         if (!map[i].parse(ini, state))
            valid = false;

         break;
      }
   }

   return valid;
}

static void
ini_data_free(struct ini_data *data)
{
   if (!data)
      return;

   chck_hash_table_for_each_call(&data->table, chck_string_release);
   chck_hash_table_release(&data->table);
   free(data);
}

static struct ini_data*
ini_data(size_t size)
{
   struct ini_data *data;
   if (!(data = calloc(1, sizeof(struct ini_data))))
      return NULL;

   if (!chck_hash_table(&data->table, 0, size, sizeof(struct chck_string)))
      goto error0;

   return data;

error0:
   free(data);
   return NULL;
}

bool
ini(struct ini *ini, char delim, size_t size, ini_throw_cb cb)
{
   assert(ini && size > 0);
   memset(ini, 0, sizeof(struct ini));

   if (!(ini->data = ini_data(size)))
      return false;

   ini->throw = cb;
   ini->delim = delim;
   return true;
}

void
ini_release(struct ini *ini)
{
   if (!ini)
      return;

   ini_data_free(ini->data);
}

void
ini_flush(struct ini *ini)
{
   assert(ini);
   chck_hash_table_for_each_call(&ini->data->table, chck_string_release);
   chck_hash_table_flush(&ini->data->table);
}

bool
ini_parse_from_memory(struct ini *ini, const char *buffer, size_t size, const struct ini_options *options)
{
   assert(ini && buffer);

   struct state state;
   memset(&state, 0, sizeof(state));
   state.line = 1;
   state.size = size;
   state.line_start = state.cursor = state.buffer = buffer;

   if (options)
      memcpy(&state.options, options, sizeof(state.options));

   return parse(ini, &state);
}

bool
ini_parse(struct ini *ini, const char *path, const struct ini_options *options)
{
   assert(ini && path);

   FILE *f;
   if (!(f = fopen(path, "rb")))
      return false;

   fseek(f, 0, SEEK_END);
   const size_t size = ftell(f);
   fseek(f, 0, SEEK_SET);

   char *buffer;
   if (!size || !(buffer = malloc(size)))
      goto error0;

   if (fread(buffer, 1, size, f) != size)
      goto error1;

   fclose(f);

   const bool ret = ini_parse_from_memory(ini, buffer, size, options);
   free(buffer);
   return ret;

error1:
   free(buffer);
error0:
   fclose(f);
   return false;
}

bool
ini_get(struct ini *ini, const char *path, struct ini_value *out_value)
{
   assert(ini && path);

   const struct chck_string *v = chck_hash_table_str_get(&ini->data->table, path, strlen(path));
   if (out_value && v) {
      out_value->data = v->data;
      out_value->size = v->size;
   }

   return (v ? true : false);
}

bool
ini_iter(struct ini *ini, struct ini_iterator *iterator, struct ini_value *out_value)
{
   assert(ini && iterator && out_value);

   if (!iterator->path)
      ini->data->iterator = (struct chck_hash_table_iterator){ &ini->data->table, 0, NULL, 0 };

   struct chck_string *v;
   if ((v = chck_hash_table_iter(&ini->data->iterator))) {
      assert(ini->data->iterator.str_key);
      iterator->path = ini->data->iterator.str_key;
      out_value->data = v->data;
      out_value->size = v->size;
   }

   return (v ? true : false);
}

void
ini_print(struct ini *ini)
{
   assert(ini);
   struct ini_value v;
   ini_for_each(ini, &v) printf("%s = %s\n", _I.path, v.data);
}

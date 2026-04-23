/*
 * File: processing.c
 * Project: The Greatest Game Calculator
 * AI Assistance Disclosure:
 * This file includes code developed with assistance from OpenAI Codex (GPT-5).
 * All generated content was reviewed and integrated by the project author.
 */

#include "processing.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz-3.1.1/miniz.h"

typedef struct LanguageCountNode {
    char *language;
    int count;
    struct LanguageCountNode *next;
} LanguageCountNode;

/* Internal helpers (file-local only). */
static void clear_review(review *r);
static char *copy_csv_field(const char *start, int len);
static GameNode *make_unknown_game(void);
static unsigned int hash_app_id(int app_id);
static double calculate_ggs(const GameNode *game);
static GameNode *create_game_node(const review *source_review);
static GameNode *get_or_create_game_node(const review *source_review);
static void update_game_from_review(GameNode *game, const review *source_review);
static GameNode *clone_game_node(const GameNode *source);
static int ascii_casecmp(const char *a, const char *b);
static char *read_file_into_heap(const char *path);
static int open_data_zip(mz_zip_archive *zip, const char *zipfilename);
static int find_csv_index_in_zip(mz_zip_archive *zip, const char *preferred_name);
static char *load_csv_text_for_passes(const char *zipfilename, const char *csvfilename);
static char *load_json_text(const char *json_filename);
static int push_review_count(WinnerStats *stats, int value);
static LanguageCountNode *find_or_create_language(LanguageCountNode **head, const char *language);
static void free_language_list(LanguageCountNode *head);
static char *title_case_copy(const char *text);
static char *choose_min_language(const LanguageCountNode *head);
static int compare_int_ascending(const void *a, const void *b);
static double compute_median_from_sorted(const int *values, int count);
static const char *skip_ws(const char *p);
static const char *find_json_object_start(const char *json, int target_app_id);
static const char *find_matching_closing_brace(const char *open_brace);
static const char *find_json_value_in_object(const char *obj_start, const char *obj_end, const char *field_name);
static char *parse_json_string_value(const char *value_start, const char *obj_end);
static int parse_json_bool_value(const char *value_start, const char *obj_end, int *out_value);
static int parse_json_number_value(const char *value_start, const char *obj_end, double *out_value);
static char *format_platforms(int supports_windows, int supports_mac, int supports_linux);

#define HASH_BUCKET_COUNT 4096
static GameNode *g_game_table[HASH_BUCKET_COUNT];

/* Returns a newly allocated copy of the input string. */
static char *dup_str(const char *s) {
    size_t len = strlen(s) + 1;
    char *out = (char *)malloc(len);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    return out;
}

/* Case-insensitive ASCII string compare similar to strcasecmp. */
static int ascii_casecmp(const char *a, const char *b) {
    if (a == NULL && b == NULL) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }

    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        int la = tolower(ca);
        int lb = tolower(cb);

        if (la != lb) {
            return (la < lb) ? -1 : 1;
        }
        a++;
        b++;
    }

    if (*a == *b) {
        return 0;
    }
    return (*a == '\0') ? -1 : 1;
}

/* Reads an entire file into heap memory and NUL-terminates the buffer. */
static char *read_file_into_heap(const char *path) {
    FILE *fp;
    long file_size_long;
    size_t file_size;
    size_t read_count;
    char *buffer;

    if (path == NULL) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_size_long = ftell(fp);
    if (file_size_long < 0) {
        fclose(fp);
        return NULL;
    }
    file_size = (size_t)file_size_long;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    read_count = fread(buffer, 1, file_size, fp);
    fclose(fp);
    if (read_count != file_size) {
        free(buffer);
        return NULL;
    }

    buffer[file_size] = '\0';
    return buffer;
}

/* Opens the exact configured zip archive path. */
static int open_data_zip(mz_zip_archive *zip, const char *zipfilename) {
    if (zip == NULL || zipfilename == NULL || zipfilename[0] == '\0') {
        return 0;
    }

    return mz_zip_reader_init_file(zip, zipfilename, 0) ? 1 : 0;
}

/* Finds the exact requested CSV entry in an opened zip archive. */
static int find_csv_index_in_zip(mz_zip_archive *zip, const char *preferred_name) {
    if (zip == NULL || preferred_name == NULL || preferred_name[0] == '\0') {
        return -1;
    }

    return mz_zip_reader_locate_file(zip, preferred_name, NULL, MZ_ZIP_FLAG_IGNORE_PATH);
}

/* Shared CSV loader for pass one and pass two (strict, macro-driven inputs). */
static char *load_csv_text_for_passes(const char *zipfilename, const char *csvfilename) {
    mz_zip_archive zip = {0};
    void *opened_file = NULL;
    size_t extracted_size = 0;
    char *csv_text = NULL;

    if (zipfilename == NULL || zipfilename[0] == '\0') {
        fprintf(stderr, "Error: ZIP filename is not configured.\n");
        return NULL;
    }
    if (csvfilename == NULL || csvfilename[0] == '\0') {
        fprintf(stderr, "Error: CSV filename is not configured.\n");
        return NULL;
    }

    if (!open_data_zip(&zip, zipfilename)) {
        fprintf(stderr, "Error: failed to open ZIP file '%s'.\n", zipfilename);
        return NULL;
    }

    {
        int csv_index = find_csv_index_in_zip(&zip, csvfilename);
        if (csv_index < 0) {
            fprintf(stderr, "Error: CSV file '%s' was not found in ZIP '%s'.\n", csvfilename, zipfilename);
            mz_zip_reader_end(&zip);
            return NULL;
        }

        opened_file = mz_zip_reader_extract_to_heap(&zip, (mz_uint)csv_index, &extracted_size, 0);
        if (opened_file == NULL) {
            fprintf(stderr, "Error: failed to extract CSV file '%s' from ZIP '%s'.\n", csvfilename, zipfilename);
            mz_zip_reader_end(&zip);
            return NULL;
        }
    }

    csv_text = (char *)malloc(extracted_size + 1);
    if (csv_text == NULL) {
        fprintf(stderr, "Error: out of memory while loading CSV '%s'.\n", csvfilename);
        mz_free(opened_file);
        mz_zip_reader_end(&zip);
        return NULL;
    }

    memcpy(csv_text, opened_file, extracted_size);
    csv_text[extracted_size] = '\0';
    mz_free(opened_file);
    mz_zip_reader_end(&zip);

    return csv_text;
}

/* Loads JSON text from the exact configured path. */
static char *load_json_text(const char *json_filename) {
    if (json_filename == NULL || json_filename[0] == '\0') {
        fprintf(stderr, "Error: JSON filename is not configured.\n");
        return NULL;
    }

    {
        char *json_text = read_file_into_heap(json_filename);
        if (json_text == NULL) {
            fprintf(stderr, "Error: failed to open JSON file '%s'.\n", json_filename);
        }
        return json_text;
    }
}

/* Appends one reviewer count to the dynamic stats array. */
static int push_review_count(WinnerStats *stats, int value) {
    if (stats == NULL) {
        return 0;
    }

    if (stats->count_size >= stats->count_capacity) {
        int new_capacity = (stats->count_capacity > 0) ? (stats->count_capacity * 2) : 128;
        int *new_buffer = (int *)realloc(stats->reviews_counts, (size_t)new_capacity * sizeof(int));
        if (new_buffer == NULL) {
            return 0;
        }
        stats->reviews_counts = new_buffer;
        stats->count_capacity = new_capacity;
    }

    stats->reviews_counts[stats->count_size++] = value;
    return 1;
}

/* Finds/creates a language counter node, normalized to lowercase ASCII. */
static LanguageCountNode *find_or_create_language(LanguageCountNode **head, const char *language) {
    LanguageCountNode *node;
    char *normalized;
    size_t raw_len;
    size_t begin = 0;
    size_t end;
    size_t i;

    if (head == NULL || language == NULL) {
        return NULL;
    }

    raw_len = strlen(language);
    while (begin < raw_len && isspace((unsigned char)language[begin])) {
        begin++;
    }
    end = raw_len;
    while (end > begin && isspace((unsigned char)language[end - 1])) {
        end--;
    }
    if (end <= begin) {
        return NULL;
    }

    normalized = (char *)malloc((end - begin) + 1);
    if (normalized == NULL) {
        return NULL;
    }
    for (i = 0; i < end - begin; i++) {
        normalized[i] = (char)tolower((unsigned char)language[begin + i]);
    }
    normalized[end - begin] = '\0';

    node = *head;
    while (node != NULL) {
        if (strcmp(node->language, normalized) == 0) {
            node->count++;
            free(normalized);
            return node;
        }
        node = node->next;
    }

    node = (LanguageCountNode *)calloc(1, sizeof(LanguageCountNode));
    if (node == NULL) {
        free(normalized);
        return NULL;
    }

    node->language = normalized;
    node->count = 1;
    node->next = *head;
    *head = node;
    return node;
}

/* Frees all nodes in the temporary language linked list. */
static void free_language_list(LanguageCountNode *head) {
    LanguageCountNode *node = head;

    while (node != NULL) {
        LanguageCountNode *next = node->next;
        free(node->language);
        free(node);
        node = next;
    }
}

/* Converts a string to title case for display output. */
static char *title_case_copy(const char *text) {
    char *out;
    int new_word = 1;
    size_t i;

    if (text == NULL) {
        return dup_str("Unknown");
    }

    out = dup_str(text);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; out[i] != '\0'; i++) {
        unsigned char c = (unsigned char)out[i];
        if (isalpha(c)) {
            out[i] = (char)(new_word ? toupper(c) : tolower(c));
            new_word = 0;
        } else {
            out[i] = (char)c;
            new_word = (c == ' ' || c == '-' || c == '_' || c == '/' || c == '.');
        }
    }

    return out;
}

/* Chooses the least frequent language with lexical tie-break. */
static char *choose_min_language(const LanguageCountNode *head) {
    const LanguageCountNode *best = NULL;
    const LanguageCountNode *node = head;

    while (node != NULL) {
        if (best == NULL ||
            node->count < best->count ||
            (node->count == best->count && strcmp(node->language, best->language) < 0)) {
            best = node;
        }
        node = node->next;
    }

    if (best == NULL) {
        return dup_str("Unknown");
    }
    return title_case_copy(best->language);
}

/* qsort comparator for integer ascending order. */
static int compare_int_ascending(const void *a, const void *b) {
    int lhs = *(const int *)a;
    int rhs = *(const int *)b;

    if (lhs < rhs) {
        return -1;
    }
    if (lhs > rhs) {
        return 1;
    }
    return 0;
}

/* Computes median from an already sorted integer array. */
static double compute_median_from_sorted(const int *values, int count) {
    if (values == NULL || count <= 0) {
        return 0.0;
    }

    if ((count % 2) == 1) {
        return (double)values[count / 2];
    }

    return ((double)values[(count / 2) - 1] + (double)values[count / 2]) / 2.0;
}

/* Skips ASCII whitespace in a NUL-terminated string. */
static const char *skip_ws(const char *p) {
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/* Finds the '{' that starts the object for a target app id key. */
static const char *find_json_object_start(const char *json, int target_app_id) {
    char key[64];
    const char *search;
    int key_len;

    if (json == NULL || target_app_id <= 0) {
        return NULL;
    }

    if (snprintf(key, sizeof(key), "\"%d\"", target_app_id) <= 0) {
        return NULL;
    }
    key_len = (int)strlen(key);
    search = json;

    while (search != NULL && *search != '\0') {
        const char *hit = strstr(search, key);
        const char *cursor;

        if (hit == NULL) {
            break;
        }

        cursor = skip_ws(hit + key_len);
        if (cursor != NULL && *cursor == ':') {
            cursor = skip_ws(cursor + 1);
            if (cursor != NULL && *cursor == '{') {
                return cursor;
            }
        }

        search = hit + 1;
    }

    return NULL;
}

/* Returns the matching closing brace for an object start. */
static const char *find_matching_closing_brace(const char *open_brace) {
    const char *p;
    int depth = 0;
    int in_string = 0;

    if (open_brace == NULL || *open_brace != '{') {
        return NULL;
    }

    for (p = open_brace; *p != '\0'; p++) {
        if (in_string) {
            if (*p == '\\') {
                if (*(p + 1) != '\0') {
                    p++;
                }
                continue;
            }
            if (*p == '"') {
                in_string = 0;
            }
            continue;
        }

        if (*p == '"') {
            in_string = 1;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }

    return NULL;
}

/* Finds the value-start pointer for one top-level field inside an object span. */
static const char *find_json_value_in_object(const char *obj_start, const char *obj_end, const char *field_name) {
    char pattern[128];
    const char *search;
    size_t pattern_len;

    if (obj_start == NULL || obj_end == NULL || field_name == NULL || obj_start >= obj_end) {
        return NULL;
    }

    if (snprintf(pattern, sizeof(pattern), "\"%s\"", field_name) <= 0) {
        return NULL;
    }
    pattern_len = strlen(pattern);
    search = obj_start;

    while (search < obj_end) {
        const char *hit = strstr(search, pattern);
        const char *cursor;

        if (hit == NULL || hit >= obj_end) {
            break;
        }

        cursor = hit + pattern_len;
        while (cursor < obj_end && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (cursor < obj_end && *cursor == ':') {
            cursor++;
            while (cursor < obj_end && isspace((unsigned char)*cursor)) {
                cursor++;
            }
            if (cursor < obj_end) {
                return cursor;
            }
        }

        search = hit + 1;
    }

    return NULL;
}

/* Parses one JSON string literal value and returns a decoded copy. */
static char *parse_json_string_value(const char *value_start, const char *obj_end) {
    const char *p;
    const char *src;
    char *out;
    char *dst;

    if (value_start == NULL || obj_end == NULL || value_start >= obj_end || *value_start != '"') {
        return NULL;
    }

    p = value_start + 1;
    while (p < obj_end && *p != '\0') {
        if (*p == '\\') {
            if ((p + 1) < obj_end) {
                p += 2;
                continue;
            }
            return NULL;
        }
        if (*p == '"') {
            break;
        }
        p++;
    }

    if (p >= obj_end || *p != '"') {
        return NULL;
    }

    out = (char *)malloc((size_t)(p - (value_start + 1)) + 1);
    if (out == NULL) {
        return NULL;
    }

    src = value_start + 1;
    dst = out;
    while (src < p) {
        if (*src == '\\' && (src + 1) < p) {
            src++;
            switch (*src) {
                case '"':  *dst++ = '"';  break;
                case '\\': *dst++ = '\\'; break;
                case '/':  *dst++ = '/';  break;
                case 'b':  *dst++ = '\b'; break;
                case 'f':  *dst++ = '\f'; break;
                case 'n':  *dst++ = '\n'; break;
                case 'r':  *dst++ = '\r'; break;
                case 't':  *dst++ = '\t'; break;
                case 'u':
                    *dst++ = '?';
                    if ((src + 4) < p) {
                        src += 4;
                    }
                    break;
                default:
                    *dst++ = *src;
                    break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return out;
}

/* Parses JSON true/false from a field value pointer. */
static int parse_json_bool_value(const char *value_start, const char *obj_end, int *out_value) {
    (void)obj_end;

    if (value_start == NULL || out_value == NULL) {
        return 0;
    }

    if (strncmp(value_start, "true", 4) == 0) {
        *out_value = 1;
        return 1;
    }
    if (strncmp(value_start, "false", 5) == 0) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

/* Parses a JSON numeric field value as double. */
static int parse_json_number_value(const char *value_start, const char *obj_end, double *out_value) {
    char *endptr = NULL;
    double parsed;

    if (value_start == NULL || obj_end == NULL || out_value == NULL) {
        return 0;
    }

    parsed = strtod(value_start, &endptr);
    if (endptr == value_start || endptr == NULL || endptr > obj_end) {
        return 0;
    }

    *out_value = parsed;
    return 1;
}

/* Formats platform booleans into the required output phrase. */
static char *format_platforms(int supports_windows, int supports_mac, int supports_linux) {
    const char *names[3];
    int count = 0;

    if (supports_windows) {
        names[count++] = "Windows";
    }
    if (supports_mac) {
        names[count++] = "Mac";
    }
    if (supports_linux) {
        names[count++] = "Linux";
    }

    if (count == 0) {
        return dup_str("Unknown");
    }
    if (count == 1) {
        return dup_str(names[0]);
    }
    if (count == 2) {
        size_t len = strlen(names[0]) + strlen(names[1]) + strlen(" and ") + 1;
        char *out = (char *)malloc(len);
        if (out == NULL) {
            return NULL;
        }
        snprintf(out, len, "%s and %s", names[0], names[1]);
        return out;
    }

    {
        size_t len = strlen(names[0]) + strlen(names[1]) + strlen(names[2]) + strlen(", , and ") + 1;
        char *out = (char *)malloc(len);
        if (out == NULL) {
            return NULL;
        }
        snprintf(out, len, "%s, %s, and %s", names[0], names[1], names[2]);
        return out;
    }
}

/* Builds a default "Unknown" game node used when no winner can be computed. */
static GameNode *make_unknown_game(void) {
    GameNode *game = (GameNode *)calloc(1, sizeof(GameNode));
    if (game == NULL) {
        return NULL;
    }

    game->app_id = 0;
    game->app_name = dup_str("Unknown");
    game->ggs = 0.0;

    if (game->app_name == NULL) {
        free(game);
        return NULL;
    }

    return game;
}

/* Maps a game app id to a stable hash-table bucket index. */
static unsigned int hash_app_id(int app_id) {
    unsigned int x = (unsigned int)app_id;

    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    return x % HASH_BUCKET_COUNT;
}

/* Computes the weighted GGS score from a game's aggregate totals. */
static double calculate_ggs(const GameNode *game) {
    if (game == NULL) {
        return 0.0;
    }

    return (0.50 * game->total_hours) +
           (0.20 * (double)game->total_recommendations) +
           (0.20 * (double)game->total_comments) +
           (0.10 * (double)game->total_keywords);
}

/* Allocates and initializes a new game node from a parsed review. */
static GameNode *create_game_node(const review *source_review) {
    GameNode *node;

    if (source_review == NULL) {
        return NULL;
    }

    node = (GameNode *)calloc(1, sizeof(GameNode));
    if (node == NULL) {
        return NULL;
    }

    node->app_id = source_review->app_id;
    node->app_name = dup_str(source_review->app_name != NULL ? source_review->app_name : "Unknown");
    if (node->app_name == NULL) {
        free(node);
        return NULL;
    }

    return node;
}

/* Finds an existing game node by app id or inserts a new one into the bucket list. */
static GameNode *get_or_create_game_node(const review *source_review) {
    unsigned int bucket_index;
    GameNode *bucket_node;
    GameNode *new_node;

    if (source_review == NULL || source_review->app_id <= 0) {
        return NULL;
    }

    bucket_index = hash_app_id(source_review->app_id);
    bucket_node = g_game_table[bucket_index];

    while (bucket_node != NULL) {
        if (bucket_node->app_id == source_review->app_id) {
            return bucket_node;
        }
        bucket_node = bucket_node->next;
    }

    new_node = create_game_node(source_review);
    if (new_node == NULL) {
        return NULL;
    }

    new_node->next = g_game_table[bucket_index];
    g_game_table[bucket_index] = new_node;
    return new_node;
}

/* Applies one review's values to the aggregate totals for its game node. */
static void update_game_from_review(GameNode *game, const review *source_review) {
    if (game == NULL || source_review == NULL) {
        return;
    }

    game->total_hours += source_review->author_total_playtime;
    game->total_recommendations += source_review->recommended ? 1 : 0;
    game->total_comments += source_review->comment_count;
    game->total_keywords += source_review->keyword_count;
    game->ggs = calculate_ggs(game);
}

/* Creates a heap-owned copy of a game node for caller-owned lifetime management. */
static GameNode *clone_game_node(const GameNode *source) {
    GameNode *copy;

    if (source == NULL) {
        return NULL;
    }

    copy = (GameNode *)calloc(1, sizeof(GameNode));
    if (copy == NULL) {
        return NULL;
    }

    copy->app_id = source->app_id;
    copy->total_hours = source->total_hours;
    copy->total_recommendations = source->total_recommendations;
    copy->total_comments = source->total_comments;
    copy->total_keywords = source->total_keywords;
    copy->ggs = source->ggs;
    copy->app_name = dup_str(source->app_name != NULL ? source->app_name : "Unknown");
    if (copy->app_name == NULL) {
        free(copy);
        return NULL;
    }

    return copy;
}

/* Parses all reviews, updates hash-table aggregates, and returns the highest-scoring game. */
GameNode* process_pass_one(const char *zipfilename, const char *csvfilename) {
    char *csv_text = NULL;
    review current_review;
    char *parse_ptr;
    int had_allocation_failure = 0;
    int i;
    GameNode *highest_ggs_in_table = NULL;
    GameNode *greatestGame = NULL;

    /* Rebuild hash table from scratch each pass. */
    free_hash_table();

    csv_text = load_csv_text_for_passes(zipfilename, csvfilename);
    if (csv_text == NULL) {
        return NULL;
    }

    review_init(&current_review);
    parse_ptr = csv_text;
    if (parse_ptr != NULL && *parse_ptr != '\0') {
        while (csv_parse(&parse_ptr, &current_review)) {
            GameNode *game = NULL;

            if (current_review.app_id <= 0) {
                continue;
            }

            game = get_or_create_game_node(&current_review);
            if (game == NULL) {
                had_allocation_failure = 1;
                break;
            }

            update_game_from_review(game, &current_review);
        }
    }
    review_cleanup(&current_review);
    free(csv_text);

    if (had_allocation_failure) {
        free_hash_table();
        return NULL;
    }

    for (i = 0; i < HASH_BUCKET_COUNT; i++) {
        GameNode *bucket_node = g_game_table[i];
        while (bucket_node != NULL) {
            bucket_node->ggs = calculate_ggs(bucket_node);
            if (highest_ggs_in_table == NULL || bucket_node->ggs > highest_ggs_in_table->ggs) {
                highest_ggs_in_table = bucket_node;
            }
            bucket_node = bucket_node->next;
        }
    }

    if (highest_ggs_in_table == NULL) {
        return make_unknown_game();
    }

    /* Return a detached copy so the caller can keep it after hash-table cleanup. */
    greatestGame = clone_game_node(highest_ggs_in_table);
    return greatestGame;
}

/* Computes winner-only stats: mean hours, median reviews, and rarest language. */
WinnerStats* process_pass_two(const char *zipfilename, const char *csvfilename, int target_app_id) {
    WinnerStats *stats = (WinnerStats *)calloc(1, sizeof(WinnerStats));
    char *csv_text = NULL;
    char *parse_ptr = NULL;
    review current_review;
    LanguageCountNode *language_head = NULL;
    double total_hours = 0.0;
    int matching_reviews = 0;
    int failed = 0;

    if (!stats) {
        return NULL;
    }

    stats->min_language = dup_str("Unknown");
    if (stats->min_language == NULL) {
        free(stats);
        return NULL;
    }

    if (target_app_id <= 0) {
        return stats;
    }

    csv_text = load_csv_text_for_passes(zipfilename, csvfilename);
    if (csv_text == NULL) {
        free(stats->reviews_counts);
        free(stats->min_language);
        free(stats);
        return NULL;
    }

    review_init(&current_review);
    parse_ptr = csv_text;
    while (csv_parse(&parse_ptr, &current_review)) {
        if (current_review.app_id != target_app_id) {
            continue;
        }

        matching_reviews++;
        total_hours += current_review.author_total_playtime;

        if (!push_review_count(stats, current_review.author_num_reviews)) {
            failed = 1;
            break;
        }

        if (current_review.language != NULL) {
            const char *lp = current_review.language;
            int has_visible_char = 0;

            while (*lp != '\0') {
                if (!isspace((unsigned char)*lp)) {
                    has_visible_char = 1;
                    break;
                }
                lp++;
            }

            if (has_visible_char && find_or_create_language(&language_head, current_review.language) == NULL) {
                failed = 1;
                break;
            }
        }
    }

    review_cleanup(&current_review);
    free(csv_text);

    if (failed) {
        free_language_list(language_head);
        free(stats->reviews_counts);
        free(stats->min_language);
        free(stats);
        return NULL;
    }

    if (matching_reviews > 0) {
        stats->mean_hours = total_hours / (double)matching_reviews;
    } else {
        stats->mean_hours = 0.0;
    }

    if (stats->count_size > 0) {
        qsort(stats->reviews_counts, (size_t)stats->count_size, sizeof(stats->reviews_counts[0]), compare_int_ascending);
        stats->median_reviews = compute_median_from_sorted(stats->reviews_counts, stats->count_size);
    } else {
        stats->median_reviews = 0.0;
    }

    {
        char *computed_language = choose_min_language(language_head);
        if (computed_language == NULL) {
            free_language_list(language_head);
            free(stats->reviews_counts);
            free(stats->min_language);
            free(stats);
            return NULL;
        }
        free(stats->min_language);
        stats->min_language = computed_language;
    }

    free_language_list(language_head);
    return stats;
}

/* Loads release date, price, and native platforms for the winner from JSON. */
GameMetadata* get_game_metadata(const char *json_filename, int target_app_id) {
    GameMetadata *metadata = (GameMetadata *)calloc(1, sizeof(GameMetadata));
    char *json_text = NULL;
    const char *obj_start;
    const char *obj_end;

    if (!metadata) {
        return NULL;
    }

    metadata->release_date = dup_str("Unknown");
    metadata->price = 0.0;
    metadata->platforms = dup_str("Unknown");
    if (metadata->release_date == NULL || metadata->platforms == NULL) {
        free(metadata->release_date);
        free(metadata->platforms);
        free(metadata);
        return NULL;
    }

    json_text = load_json_text(json_filename);
    if (json_text == NULL) {
        free(metadata->release_date);
        free(metadata->platforms);
        free(metadata);
        return NULL;
    }
    if (target_app_id <= 0) {
        free(json_text);
        return metadata;
    }

    obj_start = find_json_object_start(json_text, target_app_id);
    if (obj_start == NULL) {
        free(json_text);
        return metadata;
    }

    obj_end = find_matching_closing_brace(obj_start);
    if (obj_end == NULL) {
        free(json_text);
        return metadata;
    }

    {
        const char *value_ptr = find_json_value_in_object(obj_start, obj_end, "release_date");
        if (value_ptr != NULL) {
            char *parsed_date = parse_json_string_value(value_ptr, obj_end);
            if (parsed_date != NULL) {
                free(metadata->release_date);
                metadata->release_date = parsed_date;
            }
        }
    }

    {
        const char *value_ptr = find_json_value_in_object(obj_start, obj_end, "price");
        double parsed_price = 0.0;
        if (value_ptr != NULL && parse_json_number_value(value_ptr, obj_end, &parsed_price)) {
            metadata->price = parsed_price;
        }
    }

    {
        int supports_windows = 0;
        int supports_mac = 0;
        int supports_linux = 0;
        const char *value_ptr;
        char *parsed_platforms;

        value_ptr = find_json_value_in_object(obj_start, obj_end, "windows");
        if (value_ptr != NULL) {
            parse_json_bool_value(value_ptr, obj_end, &supports_windows);
        }

        value_ptr = find_json_value_in_object(obj_start, obj_end, "mac");
        if (value_ptr != NULL) {
            parse_json_bool_value(value_ptr, obj_end, &supports_mac);
        }

        value_ptr = find_json_value_in_object(obj_start, obj_end, "linux");
        if (value_ptr != NULL) {
            parse_json_bool_value(value_ptr, obj_end, &supports_linux);
        }

        parsed_platforms = format_platforms(supports_windows, supports_mac, supports_linux);
        if (parsed_platforms != NULL) {
            free(metadata->platforms);
            metadata->platforms = parsed_platforms;
        }
    }

    free(json_text);
    return metadata;
}

/* Parses one CSV record from the heap buffer into the reusable review struct. */
int csv_parse(char **heap_ptr, review *out_review) {
    char *p;
    int field_index = 1;

    if (heap_ptr == NULL || *heap_ptr == NULL || out_review == NULL) {
        return 0;
    }

    p = *heap_ptr;

    while (*p == '\r' || *p == '\n') {
        p++;
    }

    if (*p == '\0') {
        *heap_ptr = p;
        return 0;
    }

    clear_review(out_review);

    while (*p != '\0') {
        char *field_start = p;
        int in_quotes = 0;
        int field_len;
        char temp[128];
        int copy_len;

        while (*p != '\0') {
            if (*p == '"') {
                if (in_quotes && *(p + 1) == '"') {
                    p += 2;
                    continue;
                }

                in_quotes = !in_quotes;
                p++;
                continue;
            }

            if (!in_quotes && (*p == ',' || *p == '\n' || *p == '\r')) {
                break;
            }

            p++;
        }

        field_len = (int)(p - field_start);

        copy_len = field_len;
        if (copy_len > 127) {
            copy_len = 127;
        }

        memcpy(temp, field_start, (size_t)copy_len);
        temp[copy_len] = '\0';

        switch (field_index) {
            case 2:
                out_review->app_id = atoi(temp);
                break;

            case 3:
                out_review->app_name = copy_csv_field(field_start, field_len);
                break;

            case 5:
                out_review->language = copy_csv_field(field_start, field_len);
                break;

            case 6:
                out_review->keyword_count = count_keywords_in_field(field_start, field_len);
                break;

            case 9:
                out_review->recommended = (strcmp(temp, "True") == 0) ? 1 : 0;
                break;

            case 13:
                out_review->comment_count = atoi(temp);
                break;

            case 19:
                out_review->author_num_reviews = atoi(temp);
                break;

            case 20:
                out_review->author_total_playtime = atof(temp);
                break;

            default:
                break;
        }

        if (*p == ',') {
            p++;
            field_index++;
            continue;
        }

        if (*p == '\r') {
            p++;
            if (*p == '\n') {
                p++;
            }
            *heap_ptr = p;
            return 1;
        }

        if (*p == '\n') {
            p++;
            *heap_ptr = p;
            return 1;
        }

        if (*p == '\0') {
            *heap_ptr = p;
            return 1;
        }
    }

    *heap_ptr = p;
    return 1;
}

/* Compares two ASCII words case-insensitively over a fixed length. */
static int ascii_match_word(const char *a, const char *word, int len) {
    int i;

    for (i = 0; i < len; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cw = (unsigned char)word[i];

        if (tolower(ca) != tolower(cw)) {
            return 0;
        }
    }

    return 1;
}

/* Counts whole-word appearances of one target word inside a text span. */
static int count_word_in_span(const char *start, int len, const char *word) {
    int count = 0;
    int i;
    int wlen = (int)strlen(word);

    for (i = 0; i <= len - wlen; i++) {
        int left_ok;
        int right_ok;

        left_ok = (i == 0) || !isalnum((unsigned char)start[i - 1]);
        right_ok = (i + wlen == len) || !isalnum((unsigned char)start[i + wlen]);

        if (left_ok && right_ok && ascii_match_word(start + i, word, wlen)) {
            count++;
        }
    }

    return count;
}

/* Counts all tracked keywords in a review-text field with CSV quote handling. */
int count_keywords_in_field(const char *start, int len) {
    int total = 0;
    const char *content_start = start;
    int content_len = len;

    if (start == NULL || len <= 0) {
        return 0;
    }

    /* If field is quoted, ignore only the outer quotes for counting */
    if (len >= 2 && start[0] == '"' && start[len - 1] == '"') {
        content_start = start + 1;
        content_len = len - 2;
    }

    total += count_word_in_span(content_start, content_len, "love");
    total += count_word_in_span(content_start, content_len, "laugh");
    total += count_word_in_span(content_start, content_len, "good");
    total += count_word_in_span(content_start, content_len, "fun");
    total += count_word_in_span(content_start, content_len, "awesome");

    return total;
}

/* Frees heap fields in a review and resets the struct to defaults. */
static void clear_review(review *r) {
    if (r == NULL) {
        return;
    }

    free(r->app_name);
    free(r->language);

    r->app_name = NULL;
    r->language = NULL;
    r->app_id = 0;
    r->keyword_count = 0;
    r->recommended = 0;
    r->comment_count = 0;
    r->author_num_reviews = 0;
    r->author_total_playtime = 0.0;
}

/* Initializes a review struct to a clean zeroed state. */
void review_init(review *r) {
    if (r == NULL) {
        return;
    }

    memset(r, 0, sizeof(*r));
}

/* Releases any heap-owned fields currently stored in a review struct. */
void review_cleanup(review *r) {
    clear_review(r);
}

/* Copies a CSV field span to heap memory while unquoting and unescaping as needed. */
static char *copy_csv_field(const char *start, int len) {
    char *out;
    int i, j;

    if (start == NULL || len < 0) {
        return NULL;
    }

    out = malloc((size_t)len + 1);
    if (out == NULL) {
        return NULL;
    }

    /* Quoted field */
    if (len >= 2 && start[0] == '"' && start[len - 1] == '"') {
        i = 1;
        j = 0;

        while (i < len - 1) {
            if (start[i] == '"' && i + 1 < len - 1 && start[i + 1] == '"') {
                out[j++] = '"';
                i += 2;
            } else {
                out[j++] = start[i++];
            }
        }

        out[j] = '\0';
        return out;
    }

    /* Unquoted field */
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    return out;
}

/* Frees all nodes in every hash-table bucket and resets the table to empty. */
void free_hash_table() {
    int i;

    for (i = 0; i < HASH_BUCKET_COUNT; i++) {
        GameNode *bucket_node = g_game_table[i];

        while (bucket_node != NULL) {
            GameNode *next = bucket_node->next;
            free(bucket_node->app_name);
            free(bucket_node);
            bucket_node = next;
        }

        g_game_table[i] = NULL;
    }
}

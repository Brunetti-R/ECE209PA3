#include "processing.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz-3.1.1/miniz.h"

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
/* static void debug_print_review_and_game(const review *source_review, const GameNode *game); */

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
    mz_zip_archive zip = {0};
    size_t extracted_size = 0;
    void *opened_file = NULL;
    char *csv_text = NULL;
    review current_review;
    char *parse_ptr;
    int had_allocation_failure = 0;
    int i;
    GameNode *highest_ggs_in_table = NULL;
    GameNode *greatestGame = NULL;

    /* Rebuild hash table from scratch each pass. */
    free_hash_table();

    if (zipfilename != NULL && csvfilename != NULL) {
        char fallback1[512];
        char fallback2[512];
        int zip_opened = 0;

        if (mz_zip_reader_init_file(&zip, zipfilename, 0)) {
            zip_opened = 1;
        } else {
            snprintf(fallback1, sizeof(fallback1), "../%s", zipfilename);
            if (mz_zip_reader_init_file(&zip, fallback1, 0)) {
                zip_opened = 1;
            } else {
                snprintf(fallback2, sizeof(fallback2), "../../%s", zipfilename);
                if (mz_zip_reader_init_file(&zip, fallback2, 0)) {
                    zip_opened = 1;
                }
            }
        }

        if (zip_opened) {
        opened_file = mz_zip_reader_extract_file_to_heap(&zip, csvfilename, &extracted_size, 0);
        if (opened_file != NULL) {
            csv_text = (char *)malloc(extracted_size + 1);
            if (csv_text != NULL) {
                memcpy(csv_text, opened_file, extracted_size);
                csv_text[extracted_size] = '\0';
            }
            mz_free(opened_file);
        }
        mz_zip_reader_end(&zip);
        } else {
            fprintf(stderr, "Warning: failed to open zip file '%s'.\n",
                    zipfilename != NULL ? zipfilename : "(null)");
        }
    } else {
        fprintf(stderr, "Warning: failed to open zip file '%s'.\n",
                zipfilename != NULL ? zipfilename : "(null)");
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
            /* DEBUG TEST HOOK: review + updated game node */
            /* debug_print_review_and_game(&current_review, game); */
            /* END DEBUG TEST HOOK */
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

/* Returns placeholder winner statistics for the second processing pass. */
WinnerStats* process_pass_two(const char *filename, int target_app_id) {
    (void)filename;
    (void)target_app_id;

    WinnerStats *stats = (WinnerStats *)calloc(1, sizeof(WinnerStats));
    if (!stats) {
        return NULL;
    }

    stats->min_language = dup_str("Unknown");
    return stats;
}

/* Returns placeholder metadata for a game id from the JSON source. */
GameMetadata* get_game_metadata(const char *json_filename, int target_app_id) {
    (void)json_filename;
    (void)target_app_id;

    GameMetadata *metadata = (GameMetadata *)calloc(1, sizeof(GameMetadata));
    if (!metadata) {
        return NULL;
    }

    metadata->release_date = dup_str("Unknown");
    metadata->price = 0.0;
    metadata->platforms = dup_str("Unknown");
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

/* Prints one parsed review and the game aggregate values after its update. */
#if 0
static void debug_print_review_and_game(const review *source_review, const GameNode *game) {
    if (source_review == NULL || game == NULL) {
        return;
    }

    printf("[DEBUG][REVIEW] app_id=%d app_name=\"%s\" language=\"%s\" keywords=%d recommended=%d comments=%d author_reviews=%d author_playtime=%.3f\n",
           source_review->app_id,
           source_review->app_name != NULL ? source_review->app_name : "Unknown",
           source_review->language != NULL ? source_review->language : "Unknown",
           source_review->keyword_count,
           source_review->recommended,
           source_review->comment_count,
           source_review->author_num_reviews,
           source_review->author_total_playtime);

    printf("[DEBUG][GAME ] app_id=%d app_name=\"%s\" TH=%.3f TR=%d TC=%d TW=%d GGS=%.3f\n",
           game->app_id,
           game->app_name != NULL ? game->app_name : "Unknown",
           game->total_hours,
           game->total_recommendations,
           game->total_comments,
           game->total_keywords,
           game->ggs);
}
#endif

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

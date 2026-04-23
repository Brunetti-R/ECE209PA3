#ifndef PROCESSING_H
#define PROCESSING_H

#include "game_structs.h"

// Executes the first pass: reads CSV, builds hash table, finds max GGS.
GameNode* process_pass_one(const char *zipfilename, const char *csvfilename);

// Executes the second pass: reads CSV, filters for app_id, calculates median/language stats.
WinnerStats* process_pass_two(const char *filename, int target_app_id);

// Reads game metadata from JSON by app id.
GameMetadata* get_game_metadata(const char *json_filename, int target_app_id);

// Parses one CSV record from an in-memory buffer into out_review.
// out_review must be zero-initialized before first use.
int csv_parse(char **heap_ptr, review *out_review);

// Initializes a review object so it is safe to pass to csv_parse.
void review_init(review *r);

// Frees internal heap fields of a review parsed by csv_parse.
void review_cleanup(review *r);

// Counts keyword matches (love/laugh/good/fun/awesome) in one CSV field span.
int count_keywords_in_field(const char *start, int len);

// Frees all game nodes stored in the internal hash table.
void free_hash_table();

#endif

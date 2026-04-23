#ifndef PROCESSING_H
#define PROCESSING_H

#include "game_structs.h"

// Executes the first pass: reads CSV, builds hash table, finds max GGS.
GameNode* process_pass_one(const char *zipfilename, const char *csvfilename);

// Executes the second pass: reads CSV, filters for app_id, calculates median/language stats.
WinnerStats* process_pass_two(const char *filename, int target_app_id);

// Reads game metadata from JSON by app id.
GameMetadata* get_game_metadata(const char *json_filename, int target_app_id);

// Cleanup function
void free_hash_table();

#endif

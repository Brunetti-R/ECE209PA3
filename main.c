/*
 * File: main.c
 * Project: The Greatest Game Calculator
 * External source: miniz-3.1.1 (MIT License)
 * AI Assistance Disclosure:
 * This file includes code developed with assistance from OpenAI Codex (GPT-5).
 * All generated content was reviewed and integrated by the project author.
 * Assisted-by: OpenAI Codex:GPT-5
 */

#define ZIP_FILENAME  "test.zip"
#define CSV_FILENAME  "Test.csv"
#define JSON_FILENAME "Test.json"

#include <stdio.h>
#include <stdlib.h>
#include "processing.h"

int main() {
    const char *csv_filename = CSV_FILENAME;
    const char *json_filename = JSON_FILENAME;
    const char *zip_filename = ZIP_FILENAME;

    // --- Step 1: Aggregate Data to find the Greatest Game ---
    GameNode *greatestGame = process_pass_one(zip_filename, csv_filename);
    if (greatestGame == NULL) {
        fprintf(stderr, "Failed to process pass one.\n");
        return EXIT_FAILURE;
    }

    // --- Step 2: Detailed Analysis of the Winner ---
    WinnerStats *detailed_stats = process_pass_two(zip_filename, csv_filename, greatestGame->app_id);
    if (detailed_stats == NULL) {
        fprintf(stderr, "Failed to process pass two.\n");
        free(greatestGame->app_name);
        free(greatestGame);
        return EXIT_FAILURE;
    }

    // --- Step 3: Get Metadata from JSON file ---
    GameMetadata *metadata = get_game_metadata(zip_filename, json_filename, greatestGame->app_id);
    if (metadata == NULL) {
        fprintf(stderr, "Failed to load metadata JSON file.\n");
        free(detailed_stats->reviews_counts);
        free(detailed_stats->min_language);
        free(detailed_stats);
        free(greatestGame->app_name);
        free(greatestGame);
        free_hash_table();
        return EXIT_FAILURE;
    }

    // --- Step 4: Output Results ---
    printf("The Greatest Game Ever: %s\n",
           greatestGame->app_name != NULL ? greatestGame->app_name : "Unknown");
    printf("GGS: %.3lf\n", greatestGame->ggs);
    printf("Average Hours Played: %.0lf hours\n", detailed_stats->mean_hours);
    printf("Median Number of Reviews: %.0lf reviews\n", detailed_stats->median_reviews);
    printf("Language with Fewest Reviews: %s\n",
           detailed_stats->min_language != NULL ? detailed_stats->min_language : "Unknown");

    printf("Release Date: %s\n",
           metadata->release_date != NULL ? metadata->release_date : "Unknown");
    printf("Price: $%.2f\n", metadata->price);
    printf("Natively supported on %s\n",
           metadata->platforms != NULL ? metadata->platforms : "Unknown");

    free(detailed_stats->reviews_counts);
    free(detailed_stats->min_language);
    free(detailed_stats);

    if (metadata != NULL) {
        free(metadata->release_date);
        free(metadata->platforms);
        free(metadata);
    }

    free(greatestGame->app_name);
    free(greatestGame);
    free_hash_table();

    return 0;
}

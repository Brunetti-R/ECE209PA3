/* The Greatest Game Calculator
*
*  This program decompresses an archive and then processes
*  the data contained within to determine what was the greatest
*  game in 2021.
*
*  Your Name Here, April 2026
*  Assisted-by: 
*  Includes the  library from
*/

// macro to make my life ever so slightly easier swaping between file inputs
#define FILE_PREFIX "test"

#define ZIP_FILENAME  FILE_PREFIX ".zip"
#define CSV_FILENAME  FILE_PREFIX ".csv"
#define JSON_FILENAME "Test.json"

#include <stdio.h>
#include <stdlib.h>
#include "processing.h"


int main() {


    const char *csv_filename = CSV_FILENAME;
    const char *json_filename = "gameDescriptions.json";
    const char *zip_filename = ZIP_FILENAME;


    // --- Step 0: Ensure Data Exists ---


    // --- Step 1: Aggregate Data to find the Greatest Game ---
    GameNode *winning_game = process_pass_one(csv_filename);


    // --- Step 2: Detailed Analysis of the Winner ---
    WinnerStats *detailed_stats = process_pass_two(csv_filename, winning_game->app_id);

    // --- Step 3: Get Metadata from JSON file ---
    GameMetadata *metadata = get_game_metadata(json_filename, winning_game->app_id);

    // --- Step 4: Output Results ---
    printf("The Greatest Game Ever: %s\n", winning_game->app_name);
    printf("GGS: %.3lf\n", winning_game->ggs);
    printf("Average Hours Played: %.0lf hours\n", detailed_stats->mean_hours);
    printf("Median Number of Reviews: %.0lf reviews\n", detailed_stats->median_reviews);
    printf("Language with Fewest Reviews: %s\n", detailed_stats->min_language);

    if (metadata) {
        printf("Release Date: %s\n", metadata->release_date);
        printf("Price: $%.2f\n", metadata->price);
        printf("Natively supported on %s\n", metadata->platforms);

    } else {
        printf("Release Date: Unknown\n");
        printf("Price: Unknown\n");
        printf("Natively supported on Unknown\n");
    }

    return 0;
}
/*
 * File: game_structs.h
 * Project: The Greatest Game Calculator
 * AI Assistance Disclosure:
 * This file includes code developed with assistance from OpenAI Codex (GPT-5).
 * All generated content was reviewed and integrated by the project author.
 */

#ifndef GAME_STRUCTS_H
#define GAME_STRUCTS_H

// Represents a single game's aggregated data
typedef struct GameNode {
    int app_id;
    char *app_name;
    
    // Aggregates for GGS Calculation
    double total_hours;          // TH
    int total_recommendations;   // TR
    int total_comments;          // TC
    int total_keywords;          // TW
    
    double ggs;                  
    
    struct GameNode *next;       // For Linked List
} GameNode;

// Stats specific to the winner, collected in Step 2
typedef struct WinnerStats {
    double mean_hours;
    double median_reviews;
    char *min_language;
    
    // Temporary storage for median calculation
    int *reviews_counts;
    int count_capacity;
    int count_size;
} WinnerStats;

// Metadata from JSON
typedef struct GameMetadata {
    char *release_date;
    double price;
    char *platforms; // Formatted string "Windows, Mac, Linux"
} GameMetadata;

typedef struct review {
    int app_id;                  // Game ID used to find or create the matching GameNode
    char *app_name;              // Heap-allocated UTF-8 game title from the CSV
    char *language;              // Heap-allocated UTF-8 review language string
    int keyword_count;           // Total matches of love/laugh/good/fun/awesome in this review
    int recommended;             // 1 if review says True, 0 if False
    int comment_count;           // Number of comments on this review
    int author_num_reviews;      // Reviewer's total number of reviews on Steam
    double author_total_playtime;// Reviewer's total playtime for this game
} review;

#endif

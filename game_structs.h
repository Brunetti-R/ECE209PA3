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

#endif
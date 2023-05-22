#include <stdbool.h>
#include "dds.h"
#include <stdio.h>
#include <string.h>
#include "../include/dll.h"
#include <emscripten/emscripten.h>

// documentation link: https://github.com/dds-bridge/dds/blob/develop/doc/DLL-dds_x.pdf

// DDS suit encoding 0-spades, 1-harts, 2-diamonds, 3-clubs, 4-NT
// DDS card encoding 2=2, 3=3, ... , 13=king, 14=ace
// DDS hand encoding 0-North, 1-East, 2-South, 3-West

EMSCRIPTEN_KEEPALIVE int dds_init() 
{
    // sets both the maximum memory and the maximum number of threads. 0 - for auto config
    SetResources(40, 0);
    return 0;
}

int map_to_dds_hand(int seat)
{
    switch(seat) {
        case 0: return 2;
        case 1: return 3;
        case 2: return 0;
        case 3: return 1;
    }
    return -1;
}

void translate_current_trick_card(int rb_card, int* dds_suit, int* dds_rank) 
{
    if (rb_card < 0) 
    {
        // set to 0 because no card has been played.
        dds_suit[0] = 0;
        dds_rank[0] = 0;
        return;
    }
    
    dds_suit[0] = 3 - (rb_card / 13);   // DDS suit encoding 0-spades, 1-harts, 2-diamonds, 3-clubs, 4-NT
    dds_rank[0] = (rb_card % 13) + 2;   // DDS card encoding 2=2, 3=3, ... , 13=king, 14=ace
}


/**
@param contract_bid - the current trick suit. BBO suit encoding 3-spades, 2-harts, 1-diamonds, 0-clubs, -1-NT
@param hand_to_play - the hand leading to the trick. It should not be changed during a trick. BBO hand encoding 2-North, 3-East, 0-South, 1-West
@param currentTrick0 - the hand being played. the card suit * 13 + the card rank. BBO card and suit encoding
@param currentTrick1 - the hand being played. the card suit * 13 + the card rank. BBO card and suit encoding
@param currentTrick2 - the hand being played. the card suit * 13 + the card rank. BBO card and suit encoding
@param pbn_remain_cards - PBN formatted remain cards
@param output_array - an array of 2N integers, where N is the number of options (legal-to-play) in the hand to play.
Each pair of 2 integers represents a card and the result for this card.
*/
EMSCRIPTEN_KEEPALIVE int do_dds_solve_board(int contract_bid,
                                            int hand_to_play,
                                            int currentTrick0,
                                            int currentTrick1,
                                            int currentTrick2,
                                            char* pbn_remain_cards,
                                            int* output_array) 
{
    /**
    dealPBN 
    int trump - Suit encoding DDS suit encoding 0-spades, 1-harts, 2-diamonds, 3-clubs, 4-NT
    int first - The hand leading to the trick. DDS hand encoding 0-North, 1-East, 2-South, 3-West
    int currentTrickSuit[3] - Up to 3 cards may already have been played to the trick. Suit encoding.
    int currentTrickRank[3] - Up to 3 cards may already have been played to the trick. Hand encoding. Value range 2-14.
    Set to 0 if no card has been played.
    char remainCards[80]; Remaining cards. PBN encoding.
    */
    struct dealPBN dpbn;
    
    dpbn.first = map_to_dds_hand(hand_to_play);
    
    dpbn.trump = contract_bid == -1 ? 4 : 3 - contract_bid;
    
    translate_current_trick_card(currentTrick0, dpbn.currentTrickSuit + 0, dpbn.currentTrickRank + 0);                          
    translate_current_trick_card(currentTrick1, dpbn.currentTrickSuit + 1, dpbn.currentTrickRank + 1);                          
    translate_current_trick_card(currentTrick2, dpbn.currentTrickSuit + 2, dpbn.currentTrickRank + 2);
    
    memcpy(dpbn.remainCards, pbn_remain_cards, strlen(pbn_remain_cards) + 1);
    
    struct futureTricks ft;
    
    // The three parameters “target”, “solutions” and “mode” together control the function. 
    int target = -1;    // the number of tricks to be won (at least) by the side to play
    int solutions = 3;    // controls how many solutions should be returned
    int mode = 1;    // controls the search behavior
    
    /**
    "Target" and "solutions" work in combination. 
    The combination of "target - any" and "solutions - 3" returns all cards that can be legally played,
    with their scores in descending order.
    "Mode" is no longer used internally in DDS and has no effect.
    */

    // Invoke DDS
    SolveBoardPBN(dpbn, target, solutions, mode, &ft, 0);

   int* output_ptr = output_array;
   for (int i = 0; i < ft.cards; ++i)
   {
        *output_ptr++ = (3 - ft.suit[i]) * 13 + (ft.rank[i] - 2);
        *output_ptr++ = ft.score[i];
        
        for (int j = ft.rank[i] - 1; j >=2; --j) 
        {
            if((ft.equals[i] & (1 << j)) > 0)
            {
                *output_ptr++ = (3 - ft.suit[i]) * 13 + (j - 2);
                *output_ptr++ = ft.score[i];
            }
        }
   }
   
   while (output_ptr < output_array + 26) *output_ptr++ = -1;
   
   return ft.nodes;                          
}


/**
@param pbn_cards - the board in PBN format
@param dealer - the seat of the dealer. BBO hand formatting
@param vul - the vulnerability of the deal. DDS vulnerability formatting - 0-None, 1-Both, 2-NS only, 3-EW only
@param ddTable_output_array - an array of 5*4 integers.
Each group of 4 integers represents the highest achievable score for each seat for that trump.
Trump order - DDS suit encoding
Seat order - DDS hand encoding
@param score - par score from NS point of view
@param underOverTricks - tricks over or under 
@param level - the highest number of tricks that a partnership can win in excess of six
@param denom - the trump that wins the highest number of tricks 
@param seats - the seat of the player/players that can achieve the highest score
*/
EMSCRIPTEN_KEEPALIVE void calc_board(char* pbn_cards,
                                     int dealer,
                                     int vul,
                                     int* ddTable_output_array,
                                     int* score,
                                     int* underOverTricks,
                                     int* level,
                                     int* denom,
                                     int* seats) 
{

    struct ddTableDealPBN tableDealPBN;
    struct ddTableResults table;
    struct parResultsMaster pres;
    int DENOM_ORDER_MAP[5] = { 4, 0, 1, 2, 3 };

    strcpy(tableDealPBN.cards, pbn_cards);

    // Invoke calculation of the table score
    CalcDDtablePBN(tableDealPBN, &table);
    
    int* output_ptr = ddTable_output_array;
    for (int i=0; i<5; i++) {
        for (int j=0; j<4; j++) {
            *output_ptr++ = table.resTable[i][j];
        }
    }
    
    // Invoke PAR score
    DealerParBin(&table, &pres, map_to_dds_hand(dealer), vul);
    
    *score=pres.score;
    
    for (int i=0; i<pres.number; i++) {
        *underOverTricks= pres.contracts[i].overTricks - pres.contracts[i].underTricks;
        *level=pres.contracts[i].level;
        *denom=DENOM_ORDER_MAP[pres.contracts[i].denom];
        *seats=pres.contracts[i].seats;
    }
    return;
}
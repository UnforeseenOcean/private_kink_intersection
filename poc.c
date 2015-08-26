// Copyright (C) 2015 Teledildonics group
// Proof of concept secure kink exchange

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "crypto_hash_sha256.h"
#include "crypto_scalarmult_curve25519.h"
#include "randombytes.h"

#define MAX_STRLEN 256
#define MAX_SET_SIZE 256
#define POINT_SIZE 32
// about 8k

typedef struct {
    unsigned char encrypted_pref[POINT_SIZE];
    unsigned long offset;
} pref_t;

int compare_prefs(const void* a, const void* b){
    return memcmp(((pref_t*) a)->encrypted_pref, ((pref_t*) b)->encrypted_pref, POINT_SIZE);
}

void sort_prefs(pref_t *the_set, unsigned int the_set_size){
    qsort(the_set, the_set_size, sizeof(pref_t), compare_prefs);
}

unsigned int init(unsigned char * the_filename, pref_t *the_set, unsigned char * the_ephemeral_key){
    unsigned char pref[MAX_STRLEN];
    unsigned char hashed_pref[crypto_hash_sha256_BYTES];
    pref_t element;
    unsigned int i = 0;
    FILE* prefs;
    if(prefs = fopen(the_filename, "r")){
        while(!feof(prefs)){
            if(fgets(pref, MAX_STRLEN, prefs)){
                crypto_hash_sha256(hashed_pref, pref, strlen(pref));
                crypto_scalarmult_curve25519(the_set[i].encrypted_pref, the_ephemeral_key, hashed_pref);
                the_set[i].offset = ftell(prefs)-strlen(pref);
                i++;
            }
        }
        fclose(prefs);
    }
    return i;
}

void compare(pref_t *set_a, pref_t *set_b, unsigned char * data_file){
    int i;
    unsigned char pref[MAX_STRLEN];
    pref_t *found;
    FILE* prefs;
    for(i = 0; i < MAX_SET_SIZE; i++){
        found = bsearch(set_a+i, set_b, MAX_SET_SIZE, sizeof(pref_t), compare_prefs);
        if(found && set_a[i].offset != -1){
            if(prefs = fopen(data_file, "r")){
                fseek(prefs, set_a[i].offset, SEEK_SET);
                if(fgets(pref, MAX_STRLEN, prefs)){
                    printf("%s contains shared pref: %s", data_file, pref);
                }
                fclose(prefs);
            }
        }
    }
}

int network_ab[2], network_ba[2];

void* alice_thread(void* v){
    pref_t alices_private_set[MAX_SET_SIZE];
    pref_t bobs_private_set[MAX_SET_SIZE];
    pref_t shared_private_set[MAX_SET_SIZE];
    memset(alices_private_set, -1, sizeof(alices_private_set));
    unsigned int alices_set_size;
    unsigned char alices_ephemeral_key[POINT_SIZE];

    randombytes(alices_ephemeral_key, POINT_SIZE);

    alices_set_size = init("alice.txt", alices_private_set, alices_ephemeral_key);
    sort_prefs(alices_private_set, alices_set_size);

    int i;
    for(i = 0; i < MAX_SET_SIZE; i++){
        int sent = 0, recv = 0;
        // send our first point
        write(network_ab[1], alices_private_set[i].encrypted_pref, POINT_SIZE);

        // get it back encrypted
        read(network_ba[0], alices_private_set[i].encrypted_pref, POINT_SIZE);
        memcpy(shared_private_set[i].encrypted_pref, alices_private_set[i].encrypted_pref, POINT_SIZE);
        sort_prefs(shared_private_set, i);
        pref_t *result = bsearch(alices_private_set+i, shared_private_set, i, sizeof(pref_t), compare_prefs);
        if(result != NULL && alices_private_set[i].offset != -1 ){
            // duplicate ciphertext received, ABORT
            printf("SOMBODY IS CHEATING\n");
            exit(0);
        }

        // get their first point
        read(network_ba[0], bobs_private_set[i].encrypted_pref, POINT_SIZE);
        bobs_private_set[i].offset = 0;

        // encrypt their point
        crypto_scalarmult_curve25519(bobs_private_set[i].encrypted_pref, alices_ephemeral_key, bobs_private_set[i].encrypted_pref);
        // send it back
        write(network_ab[1], bobs_private_set[i].encrypted_pref, POINT_SIZE);
    }

    sort_prefs(bobs_private_set, MAX_SET_SIZE);
    compare(alices_private_set, bobs_private_set, "alice.txt");
}

void* bob_thread(void* v){
    pref_t bobs_private_set[MAX_SET_SIZE];
    pref_t alices_private_set[MAX_SET_SIZE];
    pref_t shared_private_set[MAX_SET_SIZE];
    memset(bobs_private_set, -1, sizeof(alices_private_set));
    unsigned int bobs_set_size;
    unsigned char bobs_ephemeral_key[POINT_SIZE];

    randombytes(bobs_ephemeral_key,POINT_SIZE);

    bobs_set_size = init("bob.txt", bobs_private_set, bobs_ephemeral_key);
    sort_prefs(bobs_private_set, bobs_set_size);

    int i;
    for(i = 0; i < MAX_SET_SIZE; i++){
        // receive Alice's first point
        read(network_ab[0], alices_private_set[i].encrypted_pref, POINT_SIZE);

        // encrypt Alice's point and send it back
        crypto_scalarmult_curve25519(alices_private_set[i].encrypted_pref, bobs_ephemeral_key, alices_private_set[i].encrypted_pref);
        write(network_ba[1], alices_private_set[i].encrypted_pref, POINT_SIZE);
        alices_private_set[i].offset = 0;

        // send Bob's point
        write(network_ba[1], bobs_private_set[i].encrypted_pref, POINT_SIZE);

        // get it back encrypted
        read(network_ab[0], bobs_private_set[i].encrypted_pref, POINT_SIZE);

        memcpy(shared_private_set[i].encrypted_pref, bobs_private_set[i].encrypted_pref, POINT_SIZE);
        sort_prefs(shared_private_set, MAX_SET_SIZE);
        pref_t *result = bsearch(bobs_private_set+i, shared_private_set, i, sizeof(pref_t), compare_prefs);
        if(result != NULL && bobs_private_set[i].offset != -1 ){
            // duplicate ciphertext received, ABORT
            printf("SOMBODY IS CHEATING\n");
            exit(0);
        }
    }

    sort_prefs(alices_private_set, MAX_SET_SIZE);
    sort_prefs(bobs_private_set, MAX_SET_SIZE);
    compare(bobs_private_set, alices_private_set, "bob.txt");
}

void main(int argc, char* argv[]){
    if(pipe(network_ab) < 0){
        perror("pipe");
    }
    if(pipe(network_ba) < 0){
        perror("pipe");
    }
    pthread_t alice, bob;
    pthread_create(&alice, NULL, alice_thread, NULL);
    pthread_create(&bob, NULL, bob_thread, NULL);
    pthread_join(alice, NULL);
    pthread_join(bob, NULL);
}

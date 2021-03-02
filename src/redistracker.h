#ifndef REDISTRACKER_H
#define REDISTRACKER_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redismodule.h"

/* ========================== Internal data structure  =======================*/
typedef struct Peer {
  uint8_t use_v4;
  uint8_t use_v6;
  uint8_t peer[6];
  uint8_t peer6[18];
} peer;

typedef struct Dict {
  RedisModuleDict *table;
  uint64_t when_to_die;
  int32_t v4_seeder;
  int32_t v6_seeder;
} dict;

typedef struct SeedersObj {
  dict *d[2];
} SeedersObj;

peer *createPeerObject(void);
void releasePeerObject(peer *o);
dict *createDictObject(void);
void releaseDictObject(dict *o);
SeedersObj *createSeedersObject(void);
void releaseSeedersObject(SeedersObj *o);

/* ========================== Common  func =============================*/
void seedersCompaction(SeedersObj *s);
int parseIPV4(RedisModuleString *str, uint8_t *res, uint8_t **has_v4);
int parseIPV6(RedisModuleString *str, uint8_t *res, uint8_t **has_v6);
void updateIP(SeedersObj *o, RedisModuleString *passkey, uint8_t *v4,
              uint8_t *v6, uint16_t port);

/* ================= "redistracker" type commands=======================*/
int RedisTrackerTypeAnnounce_RedisCommand(RedisModuleCtx *ctx,
                                          RedisModuleString **argv, int argc);

#endif
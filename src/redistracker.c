#define REDISMODULE_EXPERIMENTAL_API
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redismodule.h"

static RedisModuleType *RedisTrackerType;

/* ========================== Internal data structure  =======================*/
typedef struct Peer {
  char peer[6];
  char peer6[18];
} peer;

static peer *createPeerObject(void) {
  peer *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  return o;
}

static void releasePeerObject(peer *o) {
  if (!o) return;
  RedisModule_Free(o);
}

typedef struct Dict {
  RedisModuleDict *table;
  uint64_t when_to_die;
} dict;

static dict *createDictObject(void) {
  dict *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  o->table = RedisModule_CreateDict(NULL);
  // todo: cinfig ttl
  o->when_to_die = RedisModule_Milliseconds() / 1000 + 1800;
  return o;
}

static void releaseDictObject(dict *o) {
  if (!o) return;
  if (o->table) {
    RedisModuleDictIter *iter =
        RedisModule_DictIteratorStartC(o->table, "^", NULL, 0);
    size_t keylen;
    void *data;
    while (RedisModule_DictNextC(iter, &keylen, &data)) {
      releasePeerObject(data);
    }
    RedisModule_DictIteratorStop(iter);
    RedisModule_FreeDict(NULL, o->table);
  }
  RedisModule_Free(o);
}

typedef struct SeedersObj {
  dict *d[2];
  uint32_t seeder_count;
} SeedersObj;

static SeedersObj *createSeedersObject(void) {
  SeedersObj *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  o->d[0] = createDictObject();
  o->d[0]->when_to_die -= 1800;
  o->d[1] = createDictObject();
  o->seeder_count = 0;
  return o;
}

static void releaseSeedersObject(SeedersObj *o) {
  if (!o) return;
  if (o->d[0]) releaseDictObject(o->d[0]);
  if (o->d[1]) releaseDictObject(o->d[1]);
  RedisModule_Free(o);
}

/* ========================== Common  func =============================*/

void seedersCompaction(SeedersObj *s) {
  uint64_t now = RedisModule_Milliseconds() / 1000;
  if (now > s->d[0]->when_to_die) {
    return;
  }
  releaseDictObject(s->d[0]);
  s->d[0] = s->d[1];
  s->d[1] = createDictObject();
}

/* ================= "redistracker" type commands=======================*/

/* ANNOUNCE <info_hash> <passkey> <v4ip> <v6ip> <port> */
int RedisTrackerTypeAnnounce_RedisCommand(RedisModuleCtx *ctx,
                                          RedisModuleString **argv, int argc) {
  RedisModule_AutoMemory(ctx);
  if (argc != 6) {
    // GG
    // todo
  }
  SeedersObj *o = NULL;
  RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
  int type = RedisModule_KeyType(key);
  if (REDISMODULE_KEYTYPE_EMPTY == type) {
    o = createSeedersObject();
    RedisModule_ModuleTypeSetValue(key, RedisTrackerType, o);
  } else {
    if (RedisModule_ModuleTypeGetType(key) != RedisTrackerType) {
      RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
      return REDISMODULE_ERR;
    }
    o = RedisModule_ModuleTypeGetValue(key);
  }
  
  // todo
  return REDISMODULE_OK;
}

/* ==================== "redistracker" methods commands==================*/
void *TrackerTypeRdbLoad(RedisModuleIO *rdb, int encver) {
  REDISMODULE_NOT_USED(rdb);
  REDISMODULE_NOT_USED(encver);
  return createSeedersObject();
}

void TrackerTypeRdbSave(RedisModuleIO *rdb, void *value) {
  REDISMODULE_NOT_USED(rdb);
  REDISMODULE_NOT_USED(value);
  return;
}

void TrackerTypeAofRewrite(RedisModuleIO *aof, RedisModuleString *key,
                           void *value) {
  REDISMODULE_NOT_USED(aof);
  REDISMODULE_NOT_USED(key);
  REDISMODULE_NOT_USED(value);
  return;
}

size_t TrackerTypeMemUsage(const void *value) {
  REDISMODULE_NOT_USED(value);
  // todo
  return 114514;
}

void TrackerTypeFree(void *value) { releaseDictObject(value); }



/* This function must be present on each Redis module. It is used in order to
 * register the commands into the Redis server. */
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv,
                       int argc) {
  REDISMODULE_NOT_USED(argv);
  REDISMODULE_NOT_USED(argc);

  if (RedisModule_Init(ctx, "redistracker", 1, REDISMODULE_APIVER_1) ==
      REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "announce",
                                RedisTrackerTypeAnnounce_RedisCommand,
                                "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  RedisModuleTypeMethods tm = {
      .version = REDISMODULE_TYPE_METHOD_VERSION,
      .rdb_load = TrackerTypeRdbLoad,
      .rdb_save = TrackerTypeRdbSave,
      .aof_rewrite = TrackerTypeAofRewrite,
      .free = TrackerTypeFree,
      .digest = NULL,
      .mem_usage = TrackerTypeMemUsage,
      .aux_load = NULL,
      .aux_save = NULL,
  };
  RedisTrackerType = RedisModule_CreateDataType(ctx, "TrackType", 1, &tm);
  if (RedisTrackerType == NULL) return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
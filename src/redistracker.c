#define REDISMODULE_EXPERIMENTAL_API
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redismodule.h"

static RedisModuleType *RedisTrackerType;

static RedisModuleString *TrackerNoneString;

/* ========================== Internal data structure  =======================*/
typedef struct Peer {
  uint8_t peer[6];
  uint8_t peer6[18];
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

int parseIPV4(RedisModuleString *str, uint8_t *res) {
  if (0 == RedisModule_StringCompare(str, TrackerNoneString)) {
    memset(res, 0, 4);
    return REDISMODULE_OK;
  }
  size_t len;
  const char *s = RedisModule_StringPtrLen(str, &len);
  if (len != 4) return REDISMODULE_ERR;
  for (int i = 0; i < 4; i++) {
    res[i] = (uint8_t)s[i];
  }
  return REDISMODULE_OK;
}

int parseIPV6(RedisModuleString *str, uint8_t *res) {
  if (0 == RedisModule_StringCompare(str, TrackerNoneString)) {
    memset(res, 0, 16);
    return REDISMODULE_OK;
  }
  size_t len;
  const char *s = RedisModule_StringPtrLen(str, &len);
  if (len != 16) return REDISMODULE_ERR;
  for (int i = 0; i < 16; i++) {
    res[i] = (uint8_t)s[i];
  }
  return REDISMODULE_OK;
}

void updateIP(SeedersObj *o, RedisModuleString *passkey, uint8_t *v4,
              uint8_t *v6, uint16_t port) {
  RedisModuleDict *d2 = o->d[1]->table;
  RedisModuleDict *d1 = o->d[0]->table;
  peer *p = RedisModule_DictGet(d2, passkey, NULL);
  if (p == NULL) {
    p = RedisModule_DictGet(d1, passkey, NULL);
    if (p != NULL) {
      RedisModule_DictDel(d1, passkey, NULL);
      RedisModule_DictSet(d2, passkey, p);
    } else {
      p = createPeerObject();
      RedisModule_DictSet(d2, passkey, p);
    }
  }
  memcpy(p->peer, v4, 4);
  *(uint16_t *)(p->peer + 4) = port;
  memcpy(p->peer6, v6, 16);
  *(uint16_t *)(p->peer6 + 16) = port;
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
  uint8_t ipv4[4];
  uint8_t ipv6[16];
  int16_t port;
  int64_t tmp;
  if (parseIPV4(argv[3], ipv4) == REDISMODULE_ERR) {
    // GG
    // todo
  }
  if (parseIPV6(argv[4], ipv6) == REDISMODULE_ERR) {
    // GG
    // todo
  }
  if (RedisModule_StringToLongLong(argv[5], &tmp) && (tmp < 0 || tmp > 65535)) {
    // GG
    // todo
  }
  port = tmp % 65536;
  seedersCompaction(o);

  updateIP(o, argv[2], ipv4, ipv6, port);
  // todo: response
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

void TrackerTypeDigest(RedisModuleDigest *digest, void *value) {
  REDISMODULE_NOT_USED(value);
  REDISMODULE_NOT_USED(digest);
  // todo
}

/* This function must be present on each Redis module. It is used in order
 * to register the commands into the Redis server. */
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
      .digest = TrackerTypeDigest,
      .mem_usage = TrackerTypeMemUsage,
      .aux_load = NULL,
      .aux_save = NULL,
  };
  RedisTrackerType = RedisModule_CreateDataType(ctx, "TrackType", 1, &tm);
  TrackerNoneString = RedisModule_CreateString(NULL, "NONE", 4);
  if (RedisTrackerType == NULL) return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
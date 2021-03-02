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
  uint8_t use_v4;
  uint8_t use_v6;
  uint8_t peer[6];
  uint8_t peer6[18];
} peer;

static peer *createPeerObject(void) {
  peer *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  o->use_v4 = 0;
  o->use_v6 = 0;
  return o;
}

static void releasePeerObject(peer *o) {
  if (!o) return;
  RedisModule_Free(o);
}

typedef struct Dict {
  RedisModuleDict *table;
  uint64_t when_to_die;
  uint32_t v4_seeder;
  uint32_t v6_seeder;
} dict;

static dict *createDictObject(void) {
  dict *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  o->table = RedisModule_CreateDict(NULL);
  // todo: cinfig ttl
  o->when_to_die = RedisModule_Milliseconds() / 1000 + 1800;
  o->v4_seeder = 0;
  o->v6_seeder = 0;
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
} SeedersObj;

static SeedersObj *createSeedersObject(void) {
  SeedersObj *o;
  o = RedisModule_Calloc(1, sizeof(*o));
  o->d[0] = createDictObject();
  o->d[0]->when_to_die -= 1800;
  o->d[1] = createDictObject();
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

int _s2u(const char *start, const char *end) {
  if (start[0] == '0' && (start[1] | 0x20) == 'x') {
    int x = 0;
    for (const char *i = start + 2; i < end; i++) {
      char ch = *i;
      if (ch >= '0' && ch <= '9') {
        x = x * 16 + ch - '0';
      } else {
        ch = ch | 0x20;
        if (ch >= 'a' && ch <= 'f') {
          x = x * 16 + ch - 'a';
        } else {
          return -1;
        }
      }
    }
    return x;
  } else {
    int x = 0;
    for (const char *i = start; i < end; i++) {
      char ch = *i;
      if (ch >= '0' && ch <= '9') {
        x = x * 10 + ch - '0';
      } else {
        return -1;
      }
    }
    return x;
  }
}

int parseIPV4Inner(const char *s, size_t len, uint8_t *res) {
  size_t i, cnt = 0;
  size_t start = 0;
  for (i = 0; i <= len; i++) {
    // danger here, may overflow!
    if (i == len || s[i] == '.') {
      if (start == i || cnt > 3) {
        return REDISMODULE_ERR;
      }
      int x = _s2u(s + start, s + i);
      if (x >= 0 && x <= 255) {
        res[cnt] = (uint8_t)x;
        cnt++;
        start = i + 1;
      } else {
        return REDISMODULE_ERR;
      }
    }
  }
  if (cnt == 4) {
    return REDISMODULE_OK;
  } else {
    return REDISMODULE_ERR;
  }
}

int parseIPV4(RedisModuleString *str, uint8_t *res, uint8_t **has_v4) {
  if (0 == RedisModule_StringCompare(str, TrackerNoneString)) {
    *has_v4 = NULL;
    return REDISMODULE_OK;
  }
  size_t len;
  const char *s = RedisModule_StringPtrLen(str, &len);
  return parseIPV4Inner(s, len, res);
}

int _hex2u(const char *start, const char *end) {
  int x = 0;
  for (const char *i = start; i < end; i++) {
    char ch = *i;
    if (ch >= '0' && ch <= '9') {
      x = x * 16 + ch - '0';
    } else {
      ch = ch | 0x20;
      if (ch >= 'a' && ch <= 'f') {
        x = x * 16 + ch - 'a';
      } else {
        return -1;
      }
    }
  }
  return x;
}

int parseIPV6Inner(const char *s, size_t len, uint8_t *res) {
  size_t i, cnt = 0;
  size_t start = 0;
  for (i = 0; i <= len; i++) {
    // danger here, may overflow!
    if (i == len || s[i] == ':') {
      if (cnt > 15) {
        return REDISMODULE_ERR;
      }
      if (start == i) {
        // go reverse
        if (i == 0) {
          // like ::0:0
          i++;
        }
        for (int j = cnt; j < 16; j++) {
          res[j] = 0;
        }
        break;
      }
      int x = _hex2u(s + start, s + i);
      if (x >= 0 && x <= 65535) {
        res[cnt] = x / 256;
        cnt++;
        res[cnt] = x % 256;
        cnt++;
        start = i + 1;
      } else {
        return REDISMODULE_ERR;
      }
    }
  }
  if (cnt == 16) {
    return REDISMODULE_OK;
  }
  if (cnt < 16 && i < len) {
    // reverse
    size_t inner = i;
    size_t end = len;
    int back = 15;
    for (i = len - 1; i >= inner; i--) {
      if (s[i] == ':') {
        if (back <= (int)cnt || i + 1 == end) {
          // like 0:0::
          if (end == len && i == inner)
            return REDISMODULE_OK;
          else
            return REDISMODULE_ERR;
        }
        int x = _hex2u(s + i + 1, s + end);
        if (x >= 0 && x <= 65535) {
          res[back] = x % 256;
          back--;
          res[back] = x / 256;
          back--;
          end = i;
        } else {
          return REDISMODULE_ERR;
        }
      }
    }
    return REDISMODULE_OK;
  } else {
    return REDISMODULE_ERR;
  }
}

int parseIPV6(RedisModuleString *str, uint8_t *res, uint8_t **has_v6) {
  if (0 == RedisModule_StringCompare(str, TrackerNoneString)) {
    *has_v6 = NULL;
    return REDISMODULE_OK;
  }
  size_t len;
  const char *s = RedisModule_StringPtrLen(str, &len);
  return parseIPV6Inner(s, len, res);
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
      o->d[0]->v4_seeder -= p->use_v4;
      o->d[0]->v6_seeder -= p->use_v6;
      RedisModule_DictSet(d2, passkey, p);
    } else {
      p = createPeerObject();
      RedisModule_DictSet(d2, passkey, p);
    }
  } else {
    o->d[1]->v4_seeder -= p->use_v4;
    o->d[1]->v6_seeder -= p->use_v6;
  }

  if (v4 == NULL) {
    p->use_v4 = 0;
  } else {
    p->use_v4 = 1;
    memcpy(p->peer, v4, 4);
    *(uint16_t *)(p->peer + 4) = port;
  }
  if (v6 == NULL) {
    p->use_v6 = 0;
  } else {
    p->use_v6 = 1;
    memcpy(p->peer6, v6, 16);
    *(uint16_t *)(p->peer6 + 16) = port;
  }
  o->d[1]->v4_seeder -= p->use_v4;
  o->d[1]->v6_seeder -= p->use_v6;
}

// void genResponse(SeedersObj *o, int num_want) {}

/* ================= "redistracker" type commands=======================*/

/* ANNOUNCE <info_hash> <passkey> <v4ip> <v6ip> <port> */
int RedisTrackerTypeAnnounce_RedisCommand(RedisModuleCtx *ctx,
                                          RedisModuleString **argv, int argc) {
  RedisModule_AutoMemory(ctx);
  if (argc != 6) {
    RedisModule_ReplyWithError(ctx, "FUCK U");
    return REDISMODULE_ERR;
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
  uint8_t *v4 = ipv4, *v6 = ipv6;
  int16_t port;
  int64_t tmp;
  if (parseIPV4(argv[3], ipv4, &v4) == REDISMODULE_ERR) {
    // GG
    // todo
    RedisModule_ReplyWithError(ctx, "FUCK U");
    return REDISMODULE_ERR;
  }
  if (parseIPV6(argv[4], ipv6, &v6) == REDISMODULE_ERR) {
    // GG
    // todo
    RedisModule_ReplyWithError(ctx, "FUCK U");
    return REDISMODULE_ERR;
  }
  if (RedisModule_StringToLongLong(argv[5], &tmp) && (tmp < 0 || tmp > 65535)) {
    // GG
    // todo
    RedisModule_ReplyWithError(ctx, "FUCK U");
    return REDISMODULE_ERR;
  }
  port = tmp % 65536;
  seedersCompaction(o);

  updateIP(o, argv[2], v4, v6, port);
  // todo: response
  RedisModule_ReplyWithCString(ctx, "hello world");
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

void TrackerTypeFree(void *value) { releaseSeedersObject(value); }

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
      .mem_usage = TrackerTypeMemUsage,
      .digest = NULL,
      .aux_load = NULL,
      .aux_save = NULL,
  };
  RedisTrackerType = RedisModule_CreateDataType(ctx, "TrackType", 1, &tm);
  TrackerNoneString = RedisModule_CreateString(NULL, "NONE", 4);
  if (RedisTrackerType == NULL) return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
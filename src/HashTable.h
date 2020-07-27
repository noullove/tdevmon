#pragma once

typedef enum HashTableKeyType  HashTableKeyType;
typedef struct HashTableEntry  HashTableEntry;
typedef struct HashTableBucket HashTableBucket;
typedef struct HashTable       HashTable;

//..............................................................................

enum HashTableKeyType
{
	HashTableKeyType_Pointer,
	HashTableKeyType_String,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

enum HashTableConst
{
	HashTableConst_InitialBucketCount      = 16,
	HashTableConst_ResizeThreshold         = 75,
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct HashTableEntry
{
	struct list_head m_hashTableLink;
	struct list_head m_bucketLink;

	HashTableBucket* m_bucket;
	const void* m_key;
	void* m_value;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct HashTableBucket
{
	HashTable* m_hashTable;
	struct list_head m_entryList;
	size_t m_entryCount;
	size_t m_bucketIdx;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

struct HashTable
{
	HashTableKeyType m_keyType;
	struct list_head m_entryList;
	size_t m_entryCount;
	HashTableBucket** m_bucketArray;
	size_t m_bucketCount;
	int m_kmallocFlags;
};

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

void
HashTable_construct(
	HashTable* self,
	HashTableKeyType keyType,
	gfp_t kmallocFlags // = GFP_KERNEL
	);

void
HashTable_destruct(HashTable* self);

void
HashTable_clear(HashTable* self);

HashTableEntry*
HashTable_find(
	HashTable* self,
	const void* key
	);

static
inline
void*
HashTable_findValue(
	HashTable* self,
	const void* key
	)
{
	HashTableEntry* entry = HashTable_find(self, key);
	return entry ? entry->m_value : NULL;
}

void
HashTable_remove(
	HashTable* self,
	HashTableEntry* entry
	);

static
inline
bool
HashTable_removeKey(
	HashTable* self,
	const void* key
	)
{
	HashTableEntry* entry = HashTable_find(self, key);
	return entry ? (HashTable_remove(self, entry), true) : false;
}

HashTableEntry*
HashTable_visit(
	HashTable* self,
	const void* key
	);

HashTableEntry*
HashTable_insert(
	HashTable* self,
	const void* key,
	void* value,
	void** prevValue
	);

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

bool
HashTable_p_prepareBucketArray(HashTable* self);

HashTableEntry*
HashTable_p_findEntryInBucket(
	HashTable* self,
	HashTableBucket* bucket,
	const void* key
	);

//..............................................................................

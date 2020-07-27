#include "pch.h"
#include "HashTable.h"

//..............................................................................

size_t
djb2(
	const void* _p,
	size_t size
	)
{
	const uint8_t* p = _p;
	const uint8_t* end = p + size;

	size_t hash = 5381;

	for (; p < end; p++)
		hash = ((hash << 5) + hash) + *p; // hash * 33 + c

	return hash;
}

inline
size_t
strdjb2(const char* string)
{
	return djb2(string, string ? strlen(string) : 0);
}

//..............................................................................

void
HashTable_construct(
	HashTable* self,
	HashTableKeyType keyType,
	gfp_t kmallocFlags
	)
{
	INIT_LIST_HEAD(&self->m_entryList);
	self->m_entryCount = 0;
	self->m_bucketArray = NULL;
	self->m_bucketCount = 0;
	self->m_keyType = keyType;
	self->m_kmallocFlags = kmallocFlags;
}

void
HashTable_destruct(HashTable* self)
{
	HashTable_clear(self);

	if (self->m_bucketArray)
		kfree(self->m_bucketArray);
}

inline
size_t
HashTable_getHash(
	HashTable* self,
	const void* key
	)
{
	return self->m_keyType == HashTableKeyType_String ? strdjb2(key) : (size_t)key;
}

void
HashTable_clear(HashTable* self)
{
	size_t i;

	while (!list_empty(&self->m_entryList))
	{
		struct list_head* link = self->m_entryList.next;
		HashTableEntry* entry = container_of(link, HashTableEntry, m_hashTableLink);
		list_del(link);
		kfree(entry);
	}

	self->m_entryCount = 0;

	if (self->m_bucketCount)
	{
		for (i = 0; i < self->m_bucketCount; i++)
		{
			HashTableBucket* bucket = self->m_bucketArray[i];
			if (bucket)
			{
				self->m_bucketArray[i] = NULL;
				kfree(bucket);
			}
		}

		memset(self->m_bucketArray, 0, self->m_bucketCount * sizeof(HashTableBucket*));
	}
}

HashTableEntry*
HashTable_find(
	HashTable* self,
	const void* key
	)
{
	size_t hash;
	HashTableBucket* bucket;

	if (!self->m_bucketCount)
		return NULL;

	hash = HashTable_getHash(self, key);
	bucket = self->m_bucketArray[hash % self->m_bucketCount];
	return bucket ?	HashTable_p_findEntryInBucket(self, bucket, key) : NULL;
}

void
HashTable_remove(
	HashTable* self,
	HashTableEntry* entry
	)
{
	HashTableBucket* bucket = entry->m_bucket;
	ASSERT(bucket->m_hashTable == self && self->m_bucketArray[bucket->m_bucketIdx] == bucket);

	list_del(&entry->m_hashTableLink);
	self->m_entryCount--;

	list_del(&entry->m_bucketLink);
	bucket->m_entryCount--;

	kfree(entry);

	if (!bucket->m_entryCount)
	{
		self->m_bucketArray[bucket->m_bucketIdx] = NULL;
		kfree(bucket);
	}
}

HashTableEntry*
HashTable_visit(
	HashTable* self,
	const void* key
	)
{
	bool result;

	size_t hash;
	size_t i;
	HashTableBucket* bucket;
	HashTableEntry* entry;

	result = HashTable_p_prepareBucketArray(self);
	if (!result)
		return NULL;

	hash = HashTable_getHash(self, key);
	i = hash % self->m_bucketCount;
	bucket = self->m_bucketArray[i];
	if (!bucket)
	{
		bucket = kmalloc(sizeof(HashTableBucket), self->m_kmallocFlags);
		if (!bucket)
			return NULL;

		bucket->m_hashTable = self;
		bucket->m_bucketIdx = i;
		bucket->m_entryCount = 0;
		INIT_LIST_HEAD(&bucket->m_entryList);

		self->m_bucketArray[i] = bucket;
	}

	entry = HashTable_p_findEntryInBucket(self, bucket, key);
	if (entry)
		return entry;

	entry = kmalloc(sizeof(HashTableEntry), self->m_kmallocFlags);
	if (!entry)
		return NULL;

	entry->m_key = key;
	entry->m_bucket = bucket;
	entry->m_value = NULL;

	list_add_tail(&entry->m_hashTableLink, &self->m_entryList);
	self->m_entryCount++;

	list_add_tail(&entry->m_bucketLink, &bucket->m_entryList);
	bucket->m_entryCount++;

	return entry;
}

HashTableEntry*
HashTable_insert(
	HashTable* self,
	const void* key,
	void* value,
	void** prevValue
	)
{
	HashTableEntry* entry = HashTable_visit(self, key);
	if (!entry)
		return NULL;

	if (prevValue)
		*prevValue = entry->m_value;

	entry->m_value = value;
	return entry;
}

bool
HashTable_p_prepareBucketArray(HashTable* self)
{
	uint64_t loadFactor;

	if (!self->m_bucketCount)
	{
		size_t size = HashTableConst_InitialBucketCount * sizeof(HashTableBucket*);

		ASSERT(!self->m_bucketArray);
		self->m_bucketArray = kmalloc(size, self->m_kmallocFlags);
		if (!self->m_bucketArray)
			return false;

		memset(self->m_bucketArray, 0, size);
		self->m_bucketCount = HashTableConst_InitialBucketCount;
	}

	loadFactor = (uint64_t)self->m_entryCount * 100;
	do_div(loadFactor, self->m_bucketCount);

	if (loadFactor > HashTableConst_ResizeThreshold)
	{
		size_t count = self->m_bucketCount * 2;
		size_t size = count * sizeof(HashTableBucket*);
		size_t oldSize = self->m_bucketCount * sizeof(HashTableBucket*);
		HashTableBucket** bucketArray = kmalloc(size, self->m_kmallocFlags);
		if (!bucketArray)
			return false;

		memset(bucketArray, 0, size);
		memcpy(bucketArray, self->m_bucketArray, oldSize);
		self->m_bucketArray = bucketArray;
		self->m_bucketCount = count;
	}

	return true;
}

HashTableEntry*
HashTable_p_findEntryInBucket(
	HashTable* self,
	HashTableBucket* bucket,
	const void* key
	)
{
	struct list_head* link = bucket->m_entryList.next;

	if (self->m_keyType == HashTableKeyType_String)
	{
		for (; link != &bucket->m_entryList; link = link->next)
		{
			HashTableEntry* entry = container_of(link, HashTableEntry, m_bucketLink);
			if (strcmp(entry->m_key, key) == 0)
				return entry;
		}
	}
	else
	{
		for (; link != &bucket->m_entryList; link = link->next)
		{
			HashTableEntry* entry = container_of(link, HashTableEntry, m_bucketLink);
			if (entry->m_key == key)
				return entry;
		}
	}

	return NULL;
}

//..............................................................................

#include "pch.h"
#include "lkmUtils.h"

//..............................................................................

#ifdef CONFIG_X86

// starting with Linux 5.3, they try to prevent removal of X86_CR0_WP

static
inline
void
write_cr0_original(unsigned long val)
{
	asm volatile("mov %0,%%cr0": : "r" (val), "m" (__force_order));
}

static
inline
void
disableWriteProtection(
	const void* begin,
	const void* end,
	void* backup,
	size_t backupSize
	)
{
	ulong cr0;
	ASSERT(backupSize >= sizeof(ulong));

	cr0 = read_cr0();
	write_cr0_original(cr0 & ~X86_CR0_WP);
	*(ulong*)backup = cr0;
}

static
inline
void
restoreWriteProtection(
	const void* begin,
	const void* end,
	const void* backup,
	size_t backupSize
	)
{
	ASSERT(backupSize >= sizeof(ulong));
	write_cr0_original(*(const ulong*) backup);
}

#elif (defined CONFIG_ARM)

enum
{
#ifdef CONFIG_ARM_LPAE
	PmdFlag_WpMask     = L_PMD_SECT_RDONLY,
	PmdFlag_WpDisabled = 0,
#else
	PmdFlag_WpMask     = PMD_SECT_APX | PMD_SECT_AP_WRITE,
	PmdFlag_WpDisabled = PMD_SECT_AP_WRITE,
#endif
};

static
void
disableWriteProtectionImpl(
	bool isApply, // 'true' to apply (and back-up), 'false' to restore from back-up
	const void* begin0,
	const void* end0,
	pmdval_t* backup
	)
{
	size_t begin = ALIGN((size_t)begin0 - (SECTION_SIZE - 1), SECTION_SIZE);
	size_t end = ALIGN((size_t)end0, SECTION_SIZE);
	size_t addr;

	pgd_t* pgd;
	pud_t* pud;
	pmd_t* pmd;
	size_t pmdIdx = 0;
	pmdval_t pmdVal;

	for (addr = begin; addr < end; addr += SECTION_SIZE, backup++)
	{
		pgd = pgd_offset(current->active_mm, addr);
		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);

#ifndef CONFIG_ARM_LPAE
		pmdIdx = (addr & SECTION_SIZE) ? 1 : 0;
#endif

		if (isApply)
		{
			pmdVal = pmd_val(pmd[pmdIdx]);
			pmd[pmdIdx] = __pmd((pmdVal & ~PmdFlag_WpMask) | PmdFlag_WpDisabled);
			*backup = pmdVal;
		}
		else
		{
			pmdVal = *backup;
			pmd[pmdIdx] = __pmd(pmdVal);
		}

		flush_pmd_entry(pmd);
	}

	local_flush_tlb_all();
}

static
inline
void
disableWriteProtection(
	const void* begin,
	const void* end,
	void* backup,
	size_t backupSize
	)
{
	ASSERT(backupSize >= getWriteProtectionBackupSize(begin, end));
	disableWriteProtectionImpl(true, begin, end, (pmdval_t*)backup);
}

static
inline
void
restoreWriteProtection(
	const void* begin,
	const void* end,
	const void* backup,
	size_t backupSize
	)
{
	ASSERT(backupSize >= getWriteProtectionBackupSize(begin, end));
	disableWriteProtectionImpl(false, begin, end, (pmdval_t*)backup);
}

size_t
getWriteProtectionBackupSize(
	const void* begin0,
	const void* end0
	)
{
	size_t begin = ALIGN((size_t)begin0 - (SECTION_SIZE - 1), SECTION_SIZE);
	size_t end = ALIGN((size_t)end0, SECTION_SIZE);
	size_t sectionCount = (end - begin) / SECTION_SIZE;

	ASSERT(sectionCount);

	return sectionCount * sizeof(pmdval_t);
}

#endif

void
disablePreemptionAndWriteProtection(
	const void* begin,
	const void* end,
	void* backup,
	size_t backupSize
	)
{
	preempt_disable(); // barrier is created
	disableWriteProtection(begin, end, backup, backupSize);
}

void
restoreWriteProtectionAndPreemption(
	const void* begin,
	const void* end,
	const void* backup,
	size_t backupSize
	)
{
	restoreWriteProtection(begin, end, backup, backupSize);
	preempt_enable(); // barrier is created
}

// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .

char*
createPathString(const struct path* path)
{
	char buffer[128]; // more than enough
	char* src;
	char* dst;
	size_t size;

	src = d_path(path, buffer, sizeof(buffer) - 1);
	if (IS_ERR(src))
		return src;

	buffer[sizeof(buffer) - 1] = 0;
	size = strlen(src) + 1;
	dst = kmalloc(size, GFP_KERNEL);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	memcpy(dst, src, size);
	return dst;
}

char*
copyStringFromUser(const dm_String __user* string_u)
{
	int result;
	dm_String string;
	char* p;

	result = copy_from_user(&string, string_u, sizeof(dm_String));
	if (result != 0)
		return ERR_PTR(-EFAULT);

	p = kmalloc(string.m_length + 1, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	result = copy_from_user(p, string_u + 1, string.m_length);
	if (result != 0)
	{
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	p[string.m_length] = 0; // ensure zero-terminated
	return p;
}

int
copyStringToUser(
	dm_String __user* string_u,
	const char* p
	)
{
	int result;
	dm_String string;
	size_t stringSize;
	size_t bufferSize;

	result = copy_from_user(&string, string_u, sizeof(dm_String));
	if (result != 0)
		return -EFAULT;

	string.m_length = strlen(p);

	stringSize = string.m_length + 1;
	bufferSize = sizeof(dm_String) + stringSize;

	if (string.m_bufferSize < bufferSize)
	{
		string.m_bufferSize = bufferSize;
		result = copy_to_user(string_u, &string, sizeof(dm_String));
		return result == 0 ? -ENOBUFS : -EFAULT;
	}

	result = copy_to_user(string_u, &string, sizeof(dm_String));

	if (result == 0)
		result = copy_to_user(string_u + 1, p, stringSize);

	return result == 0 ? 0 : -EFAULT;
}

uint64_t
getTimestamp(void)
{
	enum
	{
		// epoch difference between Unix time (1 Jan 1970 00:00) and Windows time (1 Jan 1601 00:00)
		EpochDiff = 11644473600LL
	};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0))
	struct timespec tspec;
	ktime_get_real_ts(&tspec);
#else
	struct timespec64 tspec;
	ktime_get_real_ts64(&tspec);
#endif

	return (uint64_t)(tspec.tv_sec + EpochDiff) * 10000000 + tspec.tv_nsec / 100;
}

struct module*
getOwnerModule(struct file* filp)
{
	if (filp->f_op && filp->f_op->owner)
		return filp->f_op->owner;

	if (!filp->f_inode) // bummer, nowhere to go from here. but wait, is it even possible?
		return NULL;

	if (!S_ISCHR(filp->f_inode->i_mode))
	{
		printk(KERN_WARNING "tdevmon: couldn't find owner module of non-char device (%x)\n", filp->f_inode->i_mode);
		return NULL;
	}

	if (filp->f_inode->i_cdev &&
		filp->f_inode->i_cdev->owner)
		return filp->f_inode->i_cdev->owner;

	printk(
		KERN_WARNING
		"tdevmon: couldn't find owner module (cdev: %p, cdev->owner: %p); "
		"TODO: lookup via f_inode->i_rdev (%x))\n",
		filp->f_inode->i_cdev,
		filp->f_inode->i_cdev->owner,
		filp->f_inode->i_rdev
		);

	return NULL;
}

//..............................................................................

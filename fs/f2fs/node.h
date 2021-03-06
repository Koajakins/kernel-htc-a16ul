/*
 * fs/f2fs/node.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define	START_NID(nid) ((nid / NAT_ENTRY_PER_BLOCK) * NAT_ENTRY_PER_BLOCK)

#define	NAT_BLOCK_OFFSET(start_nid) (start_nid / NAT_ENTRY_PER_BLOCK)

#define FREE_NID_PAGES 4

#define DEF_RA_NID_PAGES	4	

#define MAX_RA_NODE		128

#define DEF_RAM_THRESHOLD	10

#define NATVEC_SIZE	64
#define SETVEC_SIZE	32

#define LOCKED_PAGE	1

enum {
	IS_CHECKPOINTED,	
	HAS_FSYNCED_INODE,	
	HAS_LAST_FSYNC,		
	IS_DIRTY,		
};

struct node_info {
	nid_t nid;		
	nid_t ino;		
	block_t	blk_addr;	
	unsigned char version;	
	unsigned char flag;	
};

struct nat_entry {
	struct list_head list;	
	struct node_info ni;	
};

#define nat_get_nid(nat)		(nat->ni.nid)
#define nat_set_nid(nat, n)		(nat->ni.nid = n)
#define nat_get_blkaddr(nat)		(nat->ni.blk_addr)
#define nat_set_blkaddr(nat, b)		(nat->ni.blk_addr = b)
#define nat_get_ino(nat)		(nat->ni.ino)
#define nat_set_ino(nat, i)		(nat->ni.ino = i)
#define nat_get_version(nat)		(nat->ni.version)
#define nat_set_version(nat, v)		(nat->ni.version = v)

#define inc_node_version(version)	(++version)

static inline void copy_node_info(struct node_info *dst,
						struct node_info *src)
{
	dst->nid = src->nid;
	dst->ino = src->ino;
	dst->blk_addr = src->blk_addr;
	dst->version = src->version;
	
}

static inline void set_nat_flag(struct nat_entry *ne,
				unsigned int type, bool set)
{
	unsigned char mask = 0x01 << type;
	if (set)
		ne->ni.flag |= mask;
	else
		ne->ni.flag &= ~mask;
}

static inline bool get_nat_flag(struct nat_entry *ne, unsigned int type)
{
	unsigned char mask = 0x01 << type;
	return ne->ni.flag & mask;
}

static inline void nat_reset_flag(struct nat_entry *ne)
{
	
	set_nat_flag(ne, IS_CHECKPOINTED, true);
	set_nat_flag(ne, HAS_FSYNCED_INODE, false);
	set_nat_flag(ne, HAS_LAST_FSYNC, true);
}

static inline void node_info_from_raw_nat(struct node_info *ni,
						struct f2fs_nat_entry *raw_ne)
{
	ni->ino = le32_to_cpu(raw_ne->ino);
	ni->blk_addr = le32_to_cpu(raw_ne->block_addr);
	ni->version = raw_ne->version;
}

static inline void raw_nat_from_node_info(struct f2fs_nat_entry *raw_ne,
						struct node_info *ni)
{
	raw_ne->ino = cpu_to_le32(ni->ino);
	raw_ne->block_addr = cpu_to_le32(ni->blk_addr);
	raw_ne->version = ni->version;
}

enum mem_type {
	FREE_NIDS,	
	NAT_ENTRIES,	
	DIRTY_DENTS,	
	INO_ENTRIES,	
	EXTENT_CACHE,	
	BASE_CHECK,	
};

struct nat_entry_set {
	struct list_head set_list;	
	struct list_head entry_list;	
	nid_t set;			
	unsigned int entry_cnt;		
};

enum nid_state {
	NID_NEW,	
	NID_ALLOC	
};

struct free_nid {
	struct list_head list;	
	nid_t nid;		
	int state;		
};

static inline void next_free_nid(struct f2fs_sb_info *sbi, nid_t *nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *fnid;

	spin_lock(&nm_i->free_nid_list_lock);
	if (nm_i->fcnt <= 0) {
		spin_unlock(&nm_i->free_nid_list_lock);
		return;
	}
	fnid = list_entry(nm_i->free_nid_list.next, struct free_nid, list);
	*nid = fnid->nid;
	spin_unlock(&nm_i->free_nid_list_lock);
}

static inline void get_nat_bitmap(struct f2fs_sb_info *sbi, void *addr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	memcpy(addr, nm_i->nat_bitmap, nm_i->bitmap_size);
}

static inline pgoff_t current_nat_addr(struct f2fs_sb_info *sbi, nid_t start)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	pgoff_t block_off;
	pgoff_t block_addr;
	int seg_off;

	block_off = NAT_BLOCK_OFFSET(start);
	seg_off = block_off >> sbi->log_blocks_per_seg;

	block_addr = (pgoff_t)(nm_i->nat_blkaddr +
		(seg_off << sbi->log_blocks_per_seg << 1) +
		(block_off & ((1 << sbi->log_blocks_per_seg) - 1)));

	if (f2fs_test_bit(block_off, nm_i->nat_bitmap))
		block_addr += sbi->blocks_per_seg;

	return block_addr;
}

static inline pgoff_t next_nat_addr(struct f2fs_sb_info *sbi,
						pgoff_t block_addr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	block_addr -= nm_i->nat_blkaddr;
	if ((block_addr >> sbi->log_blocks_per_seg) % 2)
		block_addr -= sbi->blocks_per_seg;
	else
		block_addr += sbi->blocks_per_seg;

	return block_addr + nm_i->nat_blkaddr;
}

static inline void set_to_next_nat(struct f2fs_nm_info *nm_i, nid_t start_nid)
{
	unsigned int block_off = NAT_BLOCK_OFFSET(start_nid);

	f2fs_change_bit(block_off, nm_i->nat_bitmap);
}

static inline void fill_node_footer(struct page *page, nid_t nid,
				nid_t ino, unsigned int ofs, bool reset)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int old_flag = 0;

	if (reset)
		memset(rn, 0, sizeof(*rn));
	else
		old_flag = le32_to_cpu(rn->footer.flag);

	rn->footer.nid = cpu_to_le32(nid);
	rn->footer.ino = cpu_to_le32(ino);

	
	rn->footer.flag = cpu_to_le32((ofs << OFFSET_BIT_SHIFT) |
					(old_flag & OFFSET_BIT_MASK));
}

static inline void copy_node_footer(struct page *dst, struct page *src)
{
	struct f2fs_node *src_rn = F2FS_NODE(src);
	struct f2fs_node *dst_rn = F2FS_NODE(dst);
	memcpy(&dst_rn->footer, &src_rn->footer, sizeof(struct node_footer));
}

static inline void fill_node_footer_blkaddr(struct page *page, block_t blkaddr)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(F2FS_P_SB(page));
	struct f2fs_node *rn = F2FS_NODE(page);

	rn->footer.cp_ver = ckpt->checkpoint_ver;
	rn->footer.next_blkaddr = cpu_to_le32(blkaddr);
}

static inline nid_t ino_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.ino);
}

static inline nid_t nid_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.nid);
}

static inline unsigned int ofs_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	unsigned flag = le32_to_cpu(rn->footer.flag);
	return flag >> OFFSET_BIT_SHIFT;
}

static inline unsigned long long cpver_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le64_to_cpu(rn->footer.cp_ver);
}

static inline block_t next_blkaddr_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.next_blkaddr);
}

static inline bool IS_DNODE(struct page *node_page)
{
	unsigned int ofs = ofs_of_node(node_page);

	if (f2fs_has_xattr_block(ofs))
		return false;

	if (ofs == 3 || ofs == 4 + NIDS_PER_BLOCK ||
			ofs == 5 + 2 * NIDS_PER_BLOCK)
		return false;
	if (ofs >= 6 + 2 * NIDS_PER_BLOCK) {
		ofs -= 6 + 2 * NIDS_PER_BLOCK;
		if (!((long int)ofs % (NIDS_PER_BLOCK + 1)))
			return false;
	}
	return true;
}

static inline void set_nid(struct page *p, int off, nid_t nid, bool i)
{
	struct f2fs_node *rn = F2FS_NODE(p);

	f2fs_wait_on_page_writeback(p, NODE);

	if (i)
		rn->i.i_nid[off - NODE_DIR1_BLOCK] = cpu_to_le32(nid);
	else
		rn->in.nid[off] = cpu_to_le32(nid);
	set_page_dirty(p);
}

static inline nid_t get_nid(struct page *p, int off, bool i)
{
	struct f2fs_node *rn = F2FS_NODE(p);

	if (i)
		return le32_to_cpu(rn->i.i_nid[off - NODE_DIR1_BLOCK]);
	return le32_to_cpu(rn->in.nid[off]);
}

static inline int is_cold_data(struct page *page)
{
	return PageChecked(page);
}

static inline void set_cold_data(struct page *page)
{
	SetPageChecked(page);
}

static inline void clear_cold_data(struct page *page)
{
	ClearPageChecked(page);
}

static inline int is_node(struct page *page, int type)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	return le32_to_cpu(rn->footer.flag) & (1 << type);
}

#define is_cold_node(page)	is_node(page, COLD_BIT_SHIFT)
#define is_fsync_dnode(page)	is_node(page, FSYNC_BIT_SHIFT)
#define is_dent_dnode(page)	is_node(page, DENT_BIT_SHIFT)

static inline void set_cold_node(struct inode *inode, struct page *page)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int flag = le32_to_cpu(rn->footer.flag);

	if (S_ISDIR(inode->i_mode))
		flag &= ~(0x1 << COLD_BIT_SHIFT);
	else
		flag |= (0x1 << COLD_BIT_SHIFT);
	rn->footer.flag = cpu_to_le32(flag);
}

static inline void set_mark(struct page *page, int mark, int type)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int flag = le32_to_cpu(rn->footer.flag);
	if (mark)
		flag |= (0x1 << type);
	else
		flag &= ~(0x1 << type);
	rn->footer.flag = cpu_to_le32(flag);
}
#define set_dentry_mark(page, mark)	set_mark(page, mark, DENT_BIT_SHIFT)
#define set_fsync_mark(page, mark)	set_mark(page, mark, FSYNC_BIT_SHIFT)

#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alejandro Perez Fernandez");


static struct kmem_cache *assoofs_inode_cache;
static DEFINE_MUTEX(assoofs_sb_lock);
static DEFINE_MUTEX(assoofs_inodes_mgmt_lock);


/*
 *  Operaciones sobre ficheros
 */

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    char *buffer;
    int nbytes;

    printk(KERN_INFO "Read request\n");

    inode_info = filp->f_path.dentry->d_inode->i_private;

    if (*ppos >= inode_info->file_size) return 0;
   
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);

    buffer = (char *)bh->b_data;
    
    nbytes = min((size_t) inode_info->file_size, len); // Hay que comparar len con el tama~no del fichero por si llegamos al    final del fichero
    copy_to_user(buf, buffer, nbytes);
    brelse(bh);

    *ppos += nbytes;
    return nbytes;

}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    char *buffer;
    struct super_block *sb;

    printk(KERN_INFO "Write request\n");

    inode_info = filp->f_path.dentry->d_inode->i_private;
    sb = filp->f_path.dentry->d_inode->i_sb;


    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
    buffer = (char *)bh->b_data;
    
    buffer += *ppos;
    copy_from_user(buffer, buf, len);
    *ppos += len;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    mutex_lock_interruptible(&assoofs_inodes_mgmt_lock);

    inode_info->file_size = *ppos;
    assoofs_save_inode_info(sb, inode_info);

   
    mutex_unlock(&assoofs_inodes_mgmt_lock);
    return len;

}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    struct inode *inode;
    struct super_block *sb;
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    
    if (ctx->pos) 
        return 0;
    printk(KERN_INFO "Iterate request\n");
    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    inode_info = inode->i_private;

    record = (struct assoofs_dir_record_entry *)bh->b_data;


    if ((!S_ISDIR(inode_info->mode))) 
        return -1;
    
    bh = sb_bread(sb, inode_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++) {
        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);
        ctx->pos += sizeof(struct assoofs_dir_record_entry);
        record++;
    }
    brelse(bh);
    printk(KERN_INFO "Iterate finish\n");
    return 0;



}

/*
 *  Operaciones sobre inodos
 */

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);

static struct inode *assoofs_get_inode(struct super_block *sb, int ino);

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);

static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);

void assoofs_save_sb_info(struct super_block *vsb);

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);



struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search);


static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};


static struct inode *assoofs_get_inode(struct super_block *sb, int ino){
    struct inode *inode;
    struct assoofs_inode_info *inode_info;

    printk(KERN_INFO "assoofs_get_inode request");

    inode_info = assoofs_get_inode_info(sb,ino);

    inode = new_inode(sb);
    
    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    
    printk(KERN_INFO "new inode created");
    if (S_ISDIR(inode_info->mode))
        inode->i_fop = &assoofs_dir_operations;
    else if (S_ISREG(inode_info->mode))
        inode->i_fop = &assoofs_file_operations;
    else
        printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");

    
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode-> i_private = inode_info;
    printk(KERN_INFO "assoofs_get_inode finsih");
    return inode;

}





struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    int i;
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;

    printk(KERN_INFO "Lookup request\n");
    
 
    
    bh = sb_bread(sb, parent_info->data_block_number);

    record = (struct assoofs_dir_record_entry *)bh->b_data;
   
    printk(KERN_INFO "Lookup in inode %lld, block %llu\n ",record->inode_no, parent_info->data_block_number);
    printk(KERN_INFO "Parent has %lld",parent_info->dir_children_count);
    for (i=0; i < parent_info->dir_children_count; i++) {
        if (!strcmp(record->filename, child_dentry->d_name.name)) {
            struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Funcion auxiliar que obtine la informacion de un inodo a partir de su numero de inodo.
            inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
            d_add(child_dentry, inode);
            printk("%s file founded (ino = %lld)",record->filename ,record->inode_no );
            return NULL;
        }
        record++;
    }

    printk(KERN_ERR "No inode found for the filename");
    return NULL;
}






static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct inode *inode;
    uint64_t count;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct buffer_head *bh;
    struct super_block *sb = dir->i_sb; // obtengo un puntero al superbloque desde dir

    printk(KERN_INFO "New file request\n");
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el numero de inodos de la informacion persistente del superbloque

    inode = new_inode(sb);

    inode->i_ino = count + 1; // Asigno numero al nuevo inodo a partir de count

    if(inode->i_ino >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "File can be created Max filesystem objects are reached");
        return -1;
    }
   
    inode_init_owner(inode, dir, mode);

    inode-> i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_fop = &assoofs_file_operations;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);


    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    

    inode_info->inode_no = inode->i_ino;
    
    d_add(dentry, inode);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);

    assoofs_add_inode_info(sb, inode_info);

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la informacion persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    mutex_lock_interruptible(&assoofs_inodes_mgmt_lock);

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    mutex_unlock(&assoofs_inodes_mgmt_lock);

    return 0;
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    struct inode *inode;
    uint64_t count;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct buffer_head *bh;
    struct super_block *sb; // obtengo un puntero al superbloque desde dir

    printk(KERN_INFO "New mkdir request\n");
    sb = dir->i_sb;
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el numero de inodos de la informacion persistente del superbloque

    inode = new_inode(sb);

    inode_init_owner(inode, dir, S_IFDIR | mode);

    inode->i_ino = count + 1; // Asigno numero al nuevo inodo a partir de count
    if(inode->i_ino >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "directory can be created Max filesystem objects are reached");
        return -1;
    }

    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_fop = &assoofs_dir_operations;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

    inode_info = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
    inode_info->dir_children_count = 0;
    inode_info->mode = S_IFDIR | mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode_info->inode_no = inode->i_ino;

    inode->i_private = inode_info;


    d_add(dentry, inode);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);

    assoofs_add_inode_info(sb, inode_info);

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la informacion persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    mutex_lock_interruptible(&assoofs_inodes_mgmt_lock);

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    mutex_unlock(&assoofs_inodes_mgmt_lock);

    return 0;
}



int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block){
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;
    if(mutex_lock_interruptible(&assoofs_sb_lock)){
        return -1;
    }
    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
        if (assoofs_sb->free_blocks & (1 << i))
            break;

    *block = i;

    assoofs_sb->free_blocks &= ~(1 << i); //MARCAR EL BLOQUE A 0
    assoofs_save_sb_info(sb);
    mutex_unlock(&assoofs_sb_lock);
    return 0;

}

void assoofs_save_sb_info(struct super_block *vsb){
    struct buffer_head *bh;
    struct assoofs_super_block *sb; // Informacion persistente del superbloque en memoria
    

    sb = vsb->s_fs_info;
    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la informacion en memoria

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    brelse(bh);
    
    

}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode){
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;
    struct assoofs_super_block_info *assoofs_sb;


    assoofs_sb = (struct assoofs_super_block_info *)sb->s_fs_info;

    if(mutex_lock_interruptible(&assoofs_inodes_mgmt_lock)){
        return ;
    }
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    
       
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    if(mutex_lock_interruptible(&assoofs_sb_lock)){
        return ;
    }

    inode_info += assoofs_sb->inodes_count;

    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);

    mutex_unlock(&assoofs_sb_lock);
    mutex_unlock(&assoofs_inodes_mgmt_lock);

    brelse(bh);

}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info){
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_pos;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    if(mutex_lock_interruptible(&assoofs_sb_lock)){
        return -1;
    }
    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);

    memcpy(inode_pos, inode_info, sizeof(*inode_pos));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    mutex_unlock(&assoofs_sb_lock);
    return 0;
}

struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search){
    uint64_t count = 0;
    while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count) {
        count++;
        start++;
    }

    if (start->inode_no == search->inode_no)
        return start;
    else
        return NULL;
}


/*
 *  Operaciones sobre el superbloque
 */

/*
* PARTE OPCIONAL CACHE DE INODOS
*/
void assoofs_destroy_inode(struct inode *inode) {
    struct assoofs_inode *inode_info = inode->i_private;
    printk(KERN_INFO "Freeing private data of inode %p ( %lu)\n", inode_info, inode->i_ino);
    kmem_cache_free(assoofs_inode_cache, inode_info);
}

static const struct super_operations assoofs_sops = {
    .destroy_inode  = assoofs_destroy_inode,
};

/*
 *  Inicialización del superbloque
 */



int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    struct inode *root_inode;
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;
    printk(KERN_INFO "assoofs_fill_super request\n");

    // 1.- Leer la información persistente del superbloque del dispositivo de bloques
   
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); 
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; 

    brelse(bh); //Liberar memoria

    // 2.- Comprobar los parámetros del superbloque
    if(assoofs_sb->magic != ASSOOFS_MAGIC || assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE){
        printk(KERN_ERR "Magic number or block size mismatch");
        return -1;
    }

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    sb->s_magic = ASSOOFS_MAGIC;
    printk(KERN_INFO "Magic number in the disk %ld\n",sb->s_magic);
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = &assoofs_sops;
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    
    root_inode = new_inode(sb);
    inode_init_owner(root_inode, NULL, S_IFDIR);
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; // numero de inodo
    root_inode->i_sb = sb; // puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; // direccion de una variable de tipo struct inode_operations previamente declarada
    root_inode->i_fop = &assoofs_dir_operations; // direccion de una variable de tipo struct file_operations previamente declarada.
                                                //En la practica tenemos 2: assoofs_dir_operations y assoofs_file_operations. La primera la utilizaremos 
                                                //cuando creemos inodos para directorios (como el directorio ra´ız) y la segunda cuando creemos inodos para ficheros.
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); // fechas.
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); // Informacion persistente del inodo

    sb->s_root = d_make_root(root_inode); //Por ser el inodo raiz

    return 0;
}




struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;

    int i;
    printk(KERN_INFO "assoofs_get_inode_info request");
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    mutex_lock_interruptible(&assoofs_inodes_mgmt_lock);
    
    for (i = 0; i < afs_sb->inodes_count; i++) {
        if (inode_info->inode_no == inode_no) {
            buffer = kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;
    }
    if(i >= afs_sb->inodes_count){
        printk(KERN_ERR "assoofs_get_inode_info: Inode not found");
    }
    brelse(bh);
    mutex_unlock(&assoofs_inodes_mgmt_lock);
    printk(KERN_INFO "assoofs_get_inode_info finish");
    return buffer;
}




/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    struct dentry *ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
     printk(KERN_INFO "assoofs_mount request\n");
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    if (IS_ERR(ret)){
        printk(KERN_ERR "Error in assoofs_mount ");
        return NULL;
    }else{
         printk(KERN_INFO "assoofs_mount completed");
         return ret;
    }
}

/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,
    .name    = "assoofs",
    .mount   = assoofs_mount,
    .kill_sb = kill_litter_super,
};

static int __init assoofs_init(void) {
    int ret = register_filesystem(&assoofs_type);
    assoofs_inode_cache = kmem_cache_create("assoofs_inode_cache", sizeof(struct assoofs_inode_info), 0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD), NULL);

    printk(KERN_INFO "assoofs_init request\n");
    // Control de errores a partir del valor de ret
    if(ret !=0){
        printk(KERN_INFO "Error initializing filesystem ");
        return -1;
    }
    printk(KERN_INFO "assoofs_init completed");
    return 0;
}

static void __exit assoofs_exit(void) { 
    int ret = unregister_filesystem(&assoofs_type);
    printk(KERN_INFO "assoofs_exit request\n");
    kmem_cache_destroy(assoofs_inode_cache);
    if(ret !=0){
        printk(KERN_INFO "Error in assoofs_exit ");
    }
    printk(KERN_INFO "assoofs_exit completed");

}

module_init(assoofs_init);
module_exit(assoofs_exit);
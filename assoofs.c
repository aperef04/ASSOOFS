#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alejandro Perez Fernandez");

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    return 0;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Write request\n");
    return 0;
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
    printk(KERN_INFO "Iterate request\n");
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");
    return NULL;
}


static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    return 0;
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    printk(KERN_INFO "assoofs_fill_super request\n");
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;

    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); 
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; 

    brelse(bh); //Liberar memoria

    // 2.- Comprobar los parámetros del superbloque
    if(assoofs_sb->magic != ASSOOFS_MAGIC || assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
        return -1;

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    sb->s_magic = ASSOOFS_MAGIC;
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = *assoofs_sops;
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    struct inode *root_inode;
    root_inode = new_inode(sb);
    inode_init_owner(root_inode, NULL, S_IFDIR);
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; // numero de inodo
    root_inode->i_sb = sb; // puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; // direccion de una variable de tipo struct inode_operations previamente declarada
    root_inode->i_fop = &assoofs_dir_operations; // direccion de una variable de tipo struct file_operations previamente declarada.
                                                //En la practica tenemos 2: assoofs_dir_operations y assoofs_file_operations. La primera la utilizaremos 
                                                //cuando creemos inodos para directorios (como el directorio ra´ız) y la segunda cuando creemos inodos para ficheros.
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); // fechas.
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); // Informaci´on persistente del inodo

    sb->s_root = d_make_root(root_inode); //Por ser el inodo raiz

    return 0;
}




struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;


    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;
    for (i = 0; i < afs_sb->inodes_count; i++) {
        if (inode_info->inode_no == inode_no) {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
    inode_info++;

    }

    brelse(bh);
    return buffer;


}




/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    printk(KERN_INFO "assoofs_mount request\n");
    struct dentry *ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    if (IS_ERR(ret)){
        printk(KERN_INFO "Error in assoofs_mount ");
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
    printk(KERN_INFO "assoofs_init request\n");
    int ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    if(ret !=0){
        printk(KERN_INFO "Error initializing filesystem ");
    }
    printk(KERN_INFO "assoofs_init completed");
}

static void __exit assoofs_exit(void) {
    printk(KERN_INFO "assoofs_exit request\n");
    int ret = unregister_filesystem(&assoofs_type);
    if(ret !=0){
        printk(KERN_INFO "Error in assoofs_exit ");
    }
    printk(KERN_INFO "assoofs_exit completed");

}

module_init(assoofs_init);
module_exit(assoofs_exit);
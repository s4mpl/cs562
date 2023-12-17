class SuperBlock(object):
    field_dict: dict

    def __init__(self):
        self.field_dict = {}

    def __str__(self):
        ret_str = ""
        for name, size in self.field_dict.items():
            ret_str += f"{name} {size}\n"
        return ret_str

class Field(object):
    def __init__(self, name, size):
        self.name = name
        self.size = size

    def __str__(self):
        return self.name

disk = open("hdd0.dsk", "rb")

boot_block = disk.read(1024)

superblock_field_list = [
    Field("num_inodes", 4),
    Field("pad0", 2),
    Field("imap_blocks", 2),
    Field("zmap_blocks", 2),
    Field("first_data_zone", 2),
    Field("log_zone_size", 2),
    Field("max_size", 4),
    Field("num_zones", 4),
    Field("magic", 2),
    Field("pad2", 2),
    Field("pad3", 2),
    Field("disk_version", 1),
    Field("pad4", 1)]

superblock = SuperBlock()
for field in superblock_field_list:
    superblock.field_dict[field.name] = int.from_bytes(disk.read(field.size), byteorder="big")
print(superblock)

exit()

# get inode takens
inode_maps = [disk.read(1024) for i in range(superblock.field_dict["imap_blocks"])]
inode_num = 0
for inode_block in inode_maps:
    for byte in inode_block:
        if byte != 0:
            for i in range(8):
                if byte & 1 << i:
                    print(f"inode {inode_num} taken")
                inode_num += 1

# get zone takens
zone_maps = [disk.read(1024) for i in range(superblock.field_dict["zmap_blocks"])]
zone_num = 0
for zone_block in zone_maps:
    for byte in zone_block:
        if byte != 0:
            for i in range(8):
                if byte & 1 << i:
                    print(f"zone {zone_num} taken")
                    pass
                zone_num += 1

disk.close()
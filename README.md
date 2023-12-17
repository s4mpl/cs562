# 3's Journal

*Git Repo Lab:* Kellan Christ, Brandan Roachell, Robert Bray, Jay Ashworth, Andrew Mueller

## December 11, 2023
Something was wrong with input_handle, so I messed around with it a bit and got it working again. I think that somehow handled was getting off and the function that we were using to pull mouse events was dying because of this, so I made the handle system more robust and now it works. I also made some modifications to the mouse get function to handle a couple of edge cases that we weren't looking for before.

-Jay

Idle proc stopped working after our new changes with the drawing application elf file. I examined the memory and realized that not all of sscratch was mapped in process memory. The reason is that the sscratch could overlap with a page boundary. The fix is to align up to the upper page boundary and check how far the sscratch address is from it. If < PAGE_SIZE, also map the upper bound into process memory. This fixed it!

-Rob

Things done today (15+ hour hackathon):
* booting screen and laid out code for more functionality in the drawing app (copy+paste)
* we discovered that a few crucial `debugf()` calls still hold our entire VirtIO system together
* Rob discovered that lists seem to mess with the stack memory and one of our variables was getting overwritten, so switching the order on the stack fixed our process issues
* with these fixes, Rob and I were able to get the console ELF working!
* found and fixed a critical bug with getting the next direct data zone of a file (Andrew accidentally left something commented out while fixing it earlier), so this fixed the PPM image reading
* Rob and I were able to load `console.elf` from disk **and run it!**
* created a very simple user space application built from my kernel version and incrementally added syscalls and tested them (using `.incbin`)
* **WE FINALLY SUCCESSFULLY READ A PPM IMAGE FROM DISK AND WROTE IT TO THE FRAMEBUFFER IN USER SPACE!!!**
* I talked with Jay about user space syscalls for getting keyboard and tablet events, integrated them in, and they work to an acceptable degree
* the application works about as well as the kernel version (slower due to input handling)
* we got it onto the file system and read it into a buffer before `elf_load()` and it worked!
* polished some things

-Brandan

## December 10, 2023
ELF console is working! We have a round robin scheduler that will run idle process when the ELF console is sleeping, and it updates sepc correctly for whatever process was on the HART before we called sched_invoke again. Also added a `sched_remove` function that the exit syscall calls to avoid scheduling this process again. Added some extra error checking to sleep syscall for seeing if the process is valid from the `get_proc_on_hart` function.

-Rob

Helped Andrew work on issues we were having with sys calls. Helped Brandan and Rob get some images onto the drive since my computer is the only one that can mount it(?). After that, I made a wrapper function to get keyboard events from the global keyboard device so that we can get it in user program.

-Jay

I added code to use both keyboard and tablet input handling for drawing and option functionality. I also merged in Andrew and Jay's file system functions (`open()`, `close()`, `read()`, `write()`) and used them to implement my PPM library for the user space application. It has its own custom integer parsing utility functions as well as some for reading and writing PPM files from and to the framebuffer (converting between RGB and RGBA). At this point it almost works, but there is a small bug. The math / indexing was fun to figure out for this part.

-Brandan

## December 9, 2023
For now, I manually started pulling events from the ring buffers to test feasibility and functionality. It seems that with a mouse, drawing is going to be hard to do since any position the cursor is at counts as an event whereas a tablet expects the touch position to be able to appear anywhere instantly, so drawing with the mouse is one continuous line of events. Also, it doesn't pick up events very quickly, so the mouse needs to move very slowly to get the desired result. Oh well, it works!

-Brandan

## December 8, 2023
Made some changes to the stat system call. I realized that st_blksize and st_blocks were being calculated incorrectly. I made changes to what I believe it should be now. I removed the calculate_st_blocks() function because I don't think it is necessary and I think my initial approach was wrong. Started working on input event grabbing for the user program.

-Jay

## December 7, 2023
Implemented read and lseek. Lseek implementation could use some improvements in terms of efficiency, but I think it's okay for now.

-Jay

## December 5, 2023
Idle process finally working!!!! I realized we were allocating a trap stack on each sched_invoke call, and the system was runnin OOM. Now, we just allocate a page-size trap stack in the init and give it to each process. We also needed to actually map the idle process instructions into the image_pages of the process. Duh! My prayers to the Supreme Being paid off ... :smiley:

-Rob

## December 4, 2023
I started putting together utility functions, structure, and logic together for the user space drawing application that I had planned out already. I'm implementing it in kernel mode first in order to determine what all we need available to the user space program as far as system calls and utility functions, especially for interacting with the file system, input devices, and GPU.

-Brandan

Implemented chmod. I figured it didnt make sense to set the file type bits with chmod, so i made it to where you can only change the permission bits.

-Jay

## December 2, 2023
I figured out the steps that I need to take for the rest of read/write as well as chmod. I need to implement them in code.

-Jay

## December 1, 2023
I finished implementing the stat function and went back to working on open/read/write/close. Andrew is working on open/close so I am focusing on read and write as they should be relatively similar.

-Jay

## November 30, 2023
I spent a while trying to step through the assembly instructions in gdb to figure out what was going wrong. I learned that we get an async timer interrupt at some point and that was not being handled. I added code to schedule a process when this happens, so the process gets rescheduled after that (since it is the only one) but then fails to be loaded properly into a state for execution.

-Brandan

I started figuring out what I needed to do for stat and implementing the data structures required. Seems like a simple function as most of the stuff is just copied from the inode. I need to figure out how to calculate st_blocks.

-Jay

## November 29, 2023
Rob and I believe we finished the ELF loader code and are back to trying to debug why processes aren't running properly so we can test it. We suspect some process info or memory mappings may not be set right.

-Brandan

After Brandan and I finished up the ELF loader, I realized we need to give a trap stack whenever we schedule each process. Also, this needs to not have PB_USER, as the kernel will use it, which I mistakenly did and had to debug. Now we're getting into the trap handler. I don't think we're actually getting into the process, because it never made it to my breakpoint.

-Rob

Added the console.elf to the file system to test that out. We also started implementing system calls for the file system, but it has not been added because we want to verify it works first before pushing it. We want to make sure what we implemented works. Part of what we started to implement is the system calls for reading files from the file system, creating the file descriptor table so that we can get the calls for open, read and write work.  Right now it's not in a state to push, because we have a lot of testing to still do, but we are hoping to get what we have so far pushed over the weekend. I also talked with Brandan about what to do with this lab, and it didn't seem there was enough to parse out for us to do much since most of it was already implemented by Brandan and Rob

-Andrew, Jay

## November 28, 2023
I started laying out ELF loader code and planning out what we need to get done tomorrow as well as for the rest of the final project.

 > NOTE: I commented out `RUN_INTERNAL_CONSOLE` in `config.h` and uncommented `.incbin` in `elfcon.S`. The latter change needs to be reverted eventually.

-Brandan

## November 20, 2023
Developed the CFS code. We couldn't really test it too much since we didn't have the process code up and running, however, we do have a lot of the CFS developed. We will say that a lot of the challenge was that thinking how the CFS should work with more than one hart is a bit more complicated than we thought. We will be working on the CFS algorithm a bit more. Another thing was that we have some questions concerning implementation, but we kinda answered some of our questions as we were thinking through the logic!. 

-Andrew, Jay

Brandan made the fix to our code to use the proper process's ptable and it was crashing on store again. I looked into it and realized we didn't give write access to that portion of physical memory when we mapped it in the user ptable. Made that change and now we get back into c_trap_handler!

-Rob

## November 19, 2023
After spending multiple days on this process malarkey, finally it seems that the SATP was causing our process frame to not be translated correctly. Checked the lecture notes again and sscratch needs to be virtual, so changed our code back to using the virtual address of the process trap frame when passing into process_asm_run. After looking with Andrew + Brandan, we thought to change the frame.satp to use the kernel MMU table. This got us into c_trap_handler!!! :tada: Now we get "Unhandled Synchronous interrupt ... ", so we have to figure this out. But on the bright side, it's at least attempting to schedule our idle process now. I used the label of idle_proc to pass sepc, so I don't think it's that part. For my sanity, I'm calling it here for tonight.

-Rob

To add on, we are unsure if this change for the process is correct since `trap_satp` is already set to use the `kernel_mmu_table`, and perhaps we just need to add to / change the existing mappings in the process' newly allocated page table instead and keep `satp` assigned to what the template had (something to do with `PB_USER`?).

At the point we get to in the code now at least, our problem could be that since we have the PLIC disabled, the interrupt is unhandled, but re-enabling the PLIC will still cause our device setup code to fail (presumably because it's not set up to be asychronous yet). That or the synchronous interrupt refers to the SBI timer's attempt to context switch, but commenting that part out doesn't seem to change anything.

-Brandan

Developed CFS code, and layed out a map on how we had to implement the algorithm. Lot of it was psuedo code that we worked off of. For some reason it was hard for me, (Andrew), to visualize how this worked, so it did delay the coding part, but we figured it out. 

-Andrew, Jay

## November 18, 2023
I tried to debug the trap store fault issue a little bit more, but from there, I just started working on the process-related system calls in `syscall.c`. It's nice that the logic was basically already provided for most of them, and I believe I know what all `exit()` needs to do from my lecture notes (assuming if it's already trapped into the OS to handle the e-call, just call `process_free()` on the process and schedule another), but I'm stuck on how to get the process pointer to that point of the code. Same for the existing logic for `sleep()`--we need access to the process that called it.

-Brandan

## November 17, 2023
Rob and I looked into it some more together. Still stuck. Not good.

-Brandan

## November 16, 2023
I continued trying to debug why our process wasn't running properly. I examined a bunch of registers and stepped through the instructions in gdb to no avail. The only explanation was the address for the `sd` instruction was invalid, but the provided template code was assigning the trap frame address as virtual, but translating it to physical didn't seem to help either (but we verified that it was within the range of the heap).

-Brandan

## November 15, 2023
I looked into the process and scheduler code that Rob added, but I could not figure out why the trap was causing this new "store page fault" (`scause` value of `f`). The registers seemed okay... :persevere:

-Brandan

## November 14, 2023
Started implementing round robin scheduling and creating idle proc. Scheduler works but trap isn't working. Gets stuck on first process that's scheduled and tries to run. Seems to get stuck on first line of the trampoline code. Is it problem that sscratch appears to be virtual? Tried translating and still crashes same line. 

-Rob

## November 10, 2023
I tested the newest version of the inode cache code, and everything somehow seems to work now. Pretty cuhraazy considering how much time we spent trying to figure it out only to not know how it's working. For now, we can move on with the scheduler and eventually come back to make functions to search the cache given a file path and implement the system calls. I have a feeling this weird problem will make a comeback...

Also, I spent many hours trying to debug my GPU string functions not updating the correct rectangles. I finally discovered that despite changing the transfer command to use the rectangle's dimensions, the flush command still seems to need the dimensions of the scanout for its rectangle or else only certain sections would redraw (the multi-line strings in particular were problematic--only the first line would draw for one of them, and for the other, the x-offset was off by a few columns, so it wasn't even consistent).

-Brandan

## November 9, 2023
For some reason now the caching is just working 100% of the time on my end... Need others to test for sanity b\c not sure that I really changed much. Also, added structs that we need for processes in process.h.

-Rob

## November 8, 2023
Figured out a workaround for the caching system. Instead of allocating a new Inode for every cache entry and storing the pointer, the Inode is just a regular struct instead of a pointer, so it's automatically allocated when we make a new cache entry. This fails about 7.5% of the time, or works 92.5% depending on what glass half kind of person you are. I prefer 92.5%. Something seems still wrong with how we handle memory in this function. We haven't had issues with kmalloc up to this point, which leads me to believe we're doing something wrong still here. But, on the bright side, we can recursively search through the cache system now, so we can make our search function and continue finishing the file system when we have time.

-Rob

## November 5, 2023
Worked with Andrew to figure out what was wrong with the map function. We realized that the memory getting passed to the map_get was a local var instead of a pointer to mem. Brandan and I wrote out the open and close sys calls (without copy_from / copy_to yet). We also spent a long time on creating a master `get_next_zone` function that will be used for both read and write. It figures out the next zone block to index into by checking each level while reading each block from the device. This took us forever, but this will be worth it because we can use this as a generic function for both read and write. We went a little cuhrazy writing this at 3am, but it's done! :sweat_smile:

-Rob

I spent many hours trying to help Andrew resolve all kinds of weird bugs with creating the inode cache. We fixed some subtle bugs with our block device functions along the way, and we think the rest of the bugs are within the cache functions themselves.

Rob and I started implementing system calls while Andrew worked on completing the inode cache system. We came up with most of the logic for `open()` and `close()` and began to think about `read()` and `write()`. I worked with Rob to implement a very beautiful function `get_next_zone()` that handles traversal of file data zones so our file descriptors can save zone offsets and use them to call the block device read/write function at the proper location relative to it. This took us a lot longer than we expected, but we think it works after a bit of testing. Post-5am is a new record for this class... and not done :sleeping::frowning: I will have nightmares about data zones for the 2 hours of sleep I plan to get tonight.

-Brandan

I went to do the cache system for the file system, but I kept running into bugs that were in other sections of the code. The cache system is very close I feel, but there is something I am not seeing, and after working on this for pretty much 20 hours straight, I can't seem to get this. I have the idea on what needs to happen, so I am trying to follow through the recursive function, but our group has a weird thing where if we comment out certain debug statements then stuff crashes, so I have to keep them in which don't really help streamline debugging. Perhaps when I am refreshed after some sleep I can get it, but Brandan, and Rob both tried to help me figure out what's going on, but to no avail.

-Andrew

## November 4, 2023
Iterating over directory entries works. I wrote code for pointer indirection but so far not really tested because dir entries are all direct in the test dsk file. I tried running it for every inode regardless of type and it crashes but not sure if this is a valid test of the iteration with any possible leftover dir code so not concluding it doesn't work yet.

-Rob

I talked with Rob about next steps and more detail about how we might approach the inode cache. We plan to use a map of structs holding an inode and the same type of map (recursively), keyed by the part of the path name at that level. So for example, the root inode's map has a pair `"home"` with a value that is the struct { inode for `"/home"` and a map containing the children of `"/home"` if it is a directory }, and that map has the pair <`"f1.txt"`, { inode for `"/home/f1.txt"`, `NULL` }>. I also discussed logic for some of the system calls, file descriptors, and how to handle the CWD with Andrew and got some structs and pseudocode down for that.

-Brandan

## November 3, 2023
Worked on some of the system calls while waiting on the cache to be finished and was a little lost. 

-Kellan

Got inodes reading with a function that gets inode struct from inode num. Checking modes works, got some directories from the test dsk file.

-Rob

## November 2, 2023
Initialized the super block by reading from block device and implemented bit checking for the bitmaps in imap / zmap.

-Rob

From what Rob wrote, I implemented helper functions to set and clear bitmap values given an inode or zone index. I also calculated and verified all the other byte offsets to the start of the zmap, inodes, and data zones based on the block size specified by `log_zone_size` in the super block.

-Brandan

## October 31, 2023
I started the Minix 3 file system by copying over the necessary structs and system call declarations into the header file and started thinking about how to structure the `.c` file and the other functions we would want for accessing and modifying inodes and zones.

-Brandan

## October 29, 2023
I tested the GPU driver code a lot more thoroughly with an infinite redraw loop and found / fixed some major (but subtle) bugs. Now our main code is able to randomly generate colors and redraw forever(?) without failure.

-Brandan

We finished the input driver today. We had a few oddities to contend with. First, we had an issue with the gpu where it wouldn't display sometimes if you move the mouse over it. This may need to be revisited later. Second, the ring library was not suited to hold our input event buffer, so we had to make some functions to convert our input events to and from char arrays. Anyways, we tested it and it seems to be functional. To actually use it, we need to go set up something to detect the interrupt from the device when the input events are written to our buffer which we can't do right now because our PLIC is disabled.  

-Jay, Andrew

Figured out the block header descriptor len was incorrect because the struct size is expanded to align to 4 bytes at the end b\c of the one byte status field. So, since the descriptor to spec is 16 bytes, hardcoded the len to 16. This works! Write is successful. Refactored `block_read` and `block_flush` to account for this. Also, made wrapper functions for read\write that will read and write partial amounts that are not aligned to a sector. This prevents 0-ing out bytes that are outside of the write size just from not being aligned, or overwriting memory outside of the given buffer on a read. These seem to be working as well. Yay! :slight_smile: UPDATE: realized didn't have functionality of writing / reading in middle of block, so added this.

-Rob

After Rob rewrote some stuff, I rechecked everything and tesed some more of different write sizes and read sizes. Appears to all be working as expected. Also went to write the block_write_zeros function from the writeup but based on the lecture we should not need that. 

-Kellan

Implemented a really really really jank terminal in the main file to test if our input driver for the keyboard was actually operational. It seems to be for basic key presses, so I will be working in implementing an actual terminal that can process user input like it is supposed to be. Right now all it does is prompt the user, and then I have a jank "sleep" loop that just adds a lot of numbers to give the user the chance to type something. After the loop is done, whatever the user typed in that time frame will be printed to the screen.

-Andrew

I merged everything from the block device and input device branches into master. I tested the block driver wrappers and found that it doesn't work after sector 0 or for writes / reads between sectors and fixed both of these cases. I cleaned up a little bit and tested the input driver using the existing test code Andrew wrote as well as making it write the keyboard event to the GPU framebuffer. Now it's testing 3 drivers at once!

-Brandan

## October 28, 2023
Came back to it after yesterday were Brandan and I looked at it super fast and Brandan found that we were setting the status descriptor address incorrectly which resolved the issue of `Bogus Virtio`. Still do not know where these invalid reads are coming from on the HDD after just doing the  the write function. I implemented the Read and flush functions based on the Virtio PDF that told me some things and how to set certain things and the fact we should be return the `VIRTIO_BLK_S_OK` instead of just true, even though its the same thing. Makes it a little easier to understand. I also tried rewriting how Rob did the write function to see if I can get any difference in output and to no luck we had the exact same output as before.

-Kellan

I discovered that the invalid reads were from forgetting to translate other descriptor addresses and fixed that. Then I spent a long while trying to debug the existing code, and I refactored a bunch to get to a state where the device was reporting `VIRTIO_BLK_S_IOERR`. Unclear whether anything is wrong on the OS end or not. I was confused while trying to set up the lengths for the header and data descriptors, since it is unclear which one the size of the read/write (aligned up by 512) belongs to. The writeup mentions that it belongs in the header, but I figured that, similar to the GPU driver, the size would be the size of the header and the data descriptor had the size of the data in question (which would make sense). Either way, it still gave this error.

-Brandan

We began implementing the input driver. Today, we read through all of the writeups and began forming a plan on how we wanted to tackle the problem. On the coding side of things, we setup our h file with the necessary structs taken from the input driver lecture. Then, we wrote an initial version of input_init that is able to query the device and pull necessary info and then we began working on getting the event buffer setup and finding how the backend virtio driver interacts with the driver/device buffers. Additionally, we set up a test event handling function to help us debug. Tomorrow (of course) we plan to finish things up as we seem to be on the right track.

-Jay, Andrew

## October 25, 2023
Came back to the block device with Rob and started looking at  it as well. Rob and I went through and trouble shot some of it to see if we could get it to recognize our HDD. After a little while we realized in the make file the line to mount the HDD was commented out. After uncommenting that, we could see the hdd in the `info block` showing us we had something, however the geometry function was getting stuck on an infinite loop. With Rob, we found the issue was how I was doing my while loop and the fact it was never entering the if statement to check the type for the device. After that we could see our `capacity`,`size_max` and `seg_max` as well as the cylinders, heads and seg_max after he had left. Also added a `device_init` function and some skeleton code to come back to a little later.

-Kellan

Wrote the code for the `block_write` function based on lecture notes / virtio spec pdf. Doesn't work, but the basic structure is down.

-Rob

## October 24, 2023
After discussing it with Abram, I learned we were having the same issue as other groups: we needed to either implement the trap handler or disable the PLIC, since currently we were getting an interrupt from the device and jumping to address 0x0 to execute instructions. After disabling the PLIC for now, the devices initialize and communicate without any crashes!

-Brandan

Started working on the block device. Implemented the `block_geometry` but did not test. Planned on coming back tomorrow to finish looking at it and continuing on it.

-Kellan

## October 19, 2023
Over the last few days, I implemented the GPU driver based on the notes and info from class. I had to look at the VirtIO spec for all the structs needed to make the pseudocode work, and then I had to implement functions that allowed for easy communication via the descriptors and rings. I had to keep in mind which descriptors were meant to be read or written to by the GPU, and I finally understood how chaining using the "next" flag worked as I went along--you don't need to put all the descriptor indices in the driver ring, it seems you simply fill the descriptor, and it will read it after the first. This is why you only increment once. The different indices (`at_idx`, `driver_idx`, and `driver->idx`) were confusing while implementing `send_gpu()`, but I think I have a much better idea now. I spent many hours trying to figure out how to properly place the data in the ring, and I soon discovered that my code actually was working roughly <2% of the time (1 in 56). I saw the beautiful blue rectangle on red background and almost cried, but I knew I wasn't done. Something is wrong with how I'm doing something, since when it doesn't work, it usually hangs right after I notify the GPU of the first command (display initialization).

An hour later, I discovered that if I move my waiting code to be directly after when I notify the GPU (without there being a return from `send_gpu()` and another function call to wait), then it seems to work most of the time. This is not great, but it's probably really close to being properly functional then... I hope. From there, I added a function to draw strings to the display. Very cool.

-Brandan

## October 11, 2023
I started implementing our job system. I have a map made for each virtio device called "jobs". Each one will fill it with it's own structs however it needs. I also added a lock to each and tested. Seems to lock/unlock correctly. Next step is to take the notify set out of the rng_fill function and create our job handler which will handle each job in the order that it came in.

-Rob

## October 10, 2023
I got some random numbers!!! I realized we were putting the request ring element into the driver ring using the incorrect instance of the index. I was using the external index instead of the ring's index. Now it seems like the request is working! NOw we need to get a job going. Also, need to implement rng_finish.

-Rob

## October 6, 2023
Brandan, Kellan and I spent some time debugging our virtio notify problem. We were not able to notify before and were crashing when we called the notify function. We realized that the notify_off_multiplier actually comes from the capability itself, not the bar offset location. This was not clear at all from the spec... :smile:

-Rob

## October 1, 2023
I reviewed the changes Rob made to the PCI system based on the feedback we got, and I merged it with where we were going with the VirtIO system. Now `pci_init()` begins the global devices list by providing the `PciDevice` and `virtio_init()` takes it from there. This allows our `pci_dispatch_irq()` function to have the IRQ stored with the device and access them all easily without re-enumerating them.

I also changed `virtio_init()` to use the same global list started by `pci_init()`.

I also merged all the code across the branches, and we are in the debugging and quality checking phase. I made some fixes to `virtio_init_notify()`.

Rob, Kellan, and I also finalized the type 3 configuration and made a change to the MMIO address dereferencing to consider a 32-bit vs. 64-bit BAR.

-Brandan

So after being a dumbass, I finally deciced to read the virtio.pdf given to us which explained how we need to do the ISR a lot better for me. Working with Rob, we realized the `reserved` part of the structure was those last 30 bits and the structure was set up for us to read in the specific bits for the `queue_interrupt` and `device_cfg_interrupt`. Realizing how this was actually set we began working on reading those to check which one was 1 and then began going over our virtqueues for the device.

-Kellan

Implemented a function called virtio_isr without realizing this is not actually part of the lab. This was a function prototype left over from Kellan's experimenting on the 30th :fearful: BUT, this will help us when we get to handling interrupts from the device. It's not finished, but on a queue interrupt, the driver's private device index is iterated over while it does not equal the device's internal index (catches up). While it is not equal, process each ring element along the way to get the descriptor from the descriptor table using the ring element id. On a config interrupt, this is not yet implemented, but I'm assuming this is device specific.

-Rob

## September 30, 2023
Set up most of virtio.c structure for the virtio_init(). I made a global list of all the virtio devices, as well as set up the common cap structure which I still have to find out if I did that correctly, but it seems correct to begin with. I will need to wait to get together when we go through the debugging process to see what needs to be modifed. I also had trouble figuring out the global list with regards to each device since I had assumed we were using all virtqueues, but that isn't the case, so that simplified what I needed to do to get the descriptor table and rings set up. I will add more to this once I collaborate further with my group. 

-Andrew

I went and implemented virtio_init_notify after discussing it with Andrew. 

-Jay

Added the virtio notify capability struct to the virtio device struct. Implemented virtio_notify based on the notes. First, the bar address is gotten by using the bar number in the capability to index into the ecam's type0 bar array. Then, check if it is 32 or 64-bit bar. Change address variables accordingly. Then the offset from the capability is added to the bar addr to get the beginning of the queue address. Lastly, the queue notify offset is multiplied by the notify offset multiplier and added to the address. This gives the notification register address of the queue we want to notify. The which_queue number is written to this address, telling the device which queue originated the notification.

-Rob

AS Rob was working on the notify I was working on the virtio_ISR, I believe the logic was sound and took me a bit to figure out that within `plic.c` was a function for the `plic_claim` which should give us the `claim_id` we need to see if it is thje 32,33,34 or 35. It is also checking to see if the `device_cfg_interupt` or `queue_interupt` is the one that has been written too. There needs to be handling for these I did not implement at the moment as I was unsure, even after reading the writeup, on how to handle them. Hopefully tomorrow when discussing with Rob or Brandan we can figure it out together.

-Kellan

## September 29, 2023
Fixed pci_dispatch_irq based on grading comments. There is now a device vector that holds device structs. Each device struct holds the ecam and isr. ISR is NULL if not a virtio device, otherwise it holds the ISR cap. When a device is initialized, its capabilities are enumerated and the isr cap is saved. The dispatch function now just uses this structure to check the interrupt fields.

-Rob

## September 24, 2023
I figured out why our bar memories were overlapping. We forgot to make the "current_address" variable in the device setup a static var :sweat_smile:. Fixed that and now our devices have unique memory addresses. I also implemented pci_dispatch_irq based on the notes to check the ISR_CFG cape. I ran it with IRQ 33 and 34 and it showed some queue_interrupts as triggered. I'm guessing this is garbage since we haven't setup virtio yet? But the function doesn't crash the OS, so that's good. Lastly, I copied some code from the notes to iterate over the capes to make sure we had at least 5 vendor-specific capes for each virtio device, and we do!

-Rob

I was doing a final inspection of the output, and I realized we had two major problems:
1) Our BARs (BAR1 and BAR4) were overlapping addresses within devices
2) Devices were not within the base + limit of the bridge it was behind

After hours of debugging, print statements, and chaos, Rob, Kellan, and I were able to figure out that one of our major issues was not aligning the new current address up to the size read from the BAR. This was causing part of our address to be cleared, and we were stuck for a long time before counting the bits and realizing why (and we forgot it was mentioned in class until then). :rage::rage::rage:

We then incorporated the bus and device numbers into the starting address of a device's memory range. We think we have given enough space to each, and now we have no overlaps!

-Brandan

## September 22, 2023
I took a look at the output and noticed that the last address printed was 0x31000000, and I saw the post on Piazza that reminded me the PCIe ECAM memory segment is only mapped to 0x30ffffff. This must be our page fault! I then checked our enumeration loop and realized we were going *cuhraaaazyyy* up to bus 255. Stopping before bus 16 puts us right where we want to be--0x30ff8000, which is one device before the end of our allocated memory segment.

-Brandan

## September 21, 2023
I reorganized some code and finished setting up device bars from where Kellan left off. I added an OR to the command reg in setup_pci_bridge to make sure we don't overwrite the rest of the command bits. I wrote two separate bar setups for the 32-bit and 64-bit bars. The code is mostly the same, but in the 32-bit version, the bar counter is not incremented because it uses one bar. Additionally, the addresses are all 32-bit since we do not want to access past the single bar. This could invalidate the address space calculation. Lastly, I uncommented the USE_PCI constant. Currently, I see devices being registered in "info pci," but an instruction page fault is occuring somewhere. I'm not sure what the cause is currently. I checked the sepc register and it's all 0, so I'm not sure what instruction caused it.

-Rob

## September 20, 2023
Pulled everything from `master` to get the up to date code and begin woring on the `pci.c` file. After reading through the pci lecture a few times and going step by step as stated, I began to enumerate the bus/device up to specified amounts as we have 256 busses and 32 device slots. Within this I began working on the checks to see if the header_type and vedor_id, as well as check for 64bit or 32bit to enumerate the BARS.

I still need to add a few handling errors and statements, I added a handling error for if we access past the `0x4FFFFFFF` address to make sure we do not go over this address space as that would not be apart of the bar. There is still a lot to be done at the moment but need to take a break before getting back on this.

Also began running into issues with my Vscode and pushing to git, sent rob my PCI file that I had done and let him upload it from there.  Hoping these issues are resolved now and will have to reupdate my branch soon as this is off the `rob_pci` branch at the moment.

-Kellan

## September 17, 2023
I merged everything to `master` earlier today and found that the OS was getting a page fault before reaching the shell when the MMU and heap were enabled (`scause` was 12). Rob and I debugged `mmu_map()` and found two minor bugs that were causing page faults to happen while mapping segments for the heap (commented on them in `mmu.c`). We also had to move `page_init()` outside `init_systems()` to happen first--before any mapping--or else the bookkeeping pages couldn't have been reserved and page tables kept overwriting each other.

From there, we actually got to the OS properly (and were ecstatic about it) and analyzed the debug output to make sure it made sense. We were initially caught off-guard by `heap_init()`, but after tracing it to `kmalloc.c`, we found that we were creating new page tables at addresses that made sense! Now we just need to test our other functions somehow, likely in `main()`.

-Brandan

Brandan and I finished testing our new mmu and page functions. We tested easy translations first to check if the heap was properly mapped. Then, we tested copy_from and copy_to by creating test 'user' pages to assign values into. The first case is a simple 1-page copy. We assigned a test value into our mapped user memory and used copy_from to copy it into the heap. We did the opposite to test copy_to. Then, we allocated two new test 'user' pages and mapped these as well. Then, we assigned two values at the page boundaries and tested copying multiple pages into the heap and vice versa. This seems to work well. At first, this two-page test didn't work. We realized our copy_from and copy_to needed to be changed from iterating over the from_itr by 8 byte increments to PAGE_SIZE increments. Originally, we thought 8 bytes was correct, but we realized that because of shifting < 12, these page entries are actually a page size apart. Finally, we tested copying only 16 bytes, and this also works. Yay! :smile:

-Rob

## September 16, 2023
I think I fixed the logic in the copy functions, mainly regarding the number of bytes to be copied on each iteration (which now takes the size into consideration). From there, I finished implementing `copy_to()`. Once we get everything merged together, we can uncomment `USE_MMU` and `USE_HEAP` and do some testing.

-Brandan

## September 15, 2023:
I had to change some of the mmu_map function, as I missed some of the stuff on the writeup. Finalized the function

-Andrew

## September 14, 2023:
I finished up with my implementation of the mmu_translate and mmu_free code and pushed it to the master branch. 

-Jay

I got some help from Brandan, and finished up the mmu_map function. Pushed it to the main branch

-Andrew 

## September 13, 2023:
I went through and wrote out some pseudocode for mmu_translate and mmu_free and started on the implementation of translate. 

-Jay

I wrote up most of the mmu_map function, I had some trouble doing some of it. 

-Andrew

Brandan and I discussed the logic for copy_from and copy_to, as well as the process for mmu_access_ok. In mmu_access_ok, we need to search for the leaf node in order to access the correct permissions. Whenever moving down a level in page tables, we need to shift left 2 bits in order to preserve the PPNs in the entry when zeroing out the 12 bits for page alignment. We also realized we need to check for a page fault from mmu_translate during the copy_from function. We collaboratively wrote the mmu_access_ok function and finished out the copy_from function in uaccess (both untested). Next steps for us will be to modify the copy_from code to work with copy_to and test these thoroughly once mmu functions are ready.

-Rob

I cleaned up what we wrote so far to the point where it would `make run` again, and while starting `copy_to()`, I realized there might be some flaws in our `copy_from()` logic to revisit. I think I do actually understand the point of these functions now.

-Brandan

## September 8, 2023
I thoroughly tested and fixed some minor bugs in `page_free()` and `map_page_to_bk()` as well as added the inability to free pages that aren't the first one in an allocated chunk.

-Brandan

## September 7, 2023
Implemented page_free function using the functions made by Brandan on the `map_page_to_bk()` as well as the BK_test bytes. Based on what was said in class and what I read online and in the book on the page_free function and talking to Rob as well, we needed a check in there as well so we did not access memory we should not have due to the '-1' that gets returned in the `map_page_to_bk()` function.  Only thing left to do is to test this and make sure the function is now working properly.

-Kellan

Tested bookkeeping functions with page_nalloc, looks like it works. Also, implemented and tested counting functions for free / taken pages.

-Rob

## September 6, 2023
I implemented the bookkeeping utility functions and tested them by printing out individual bytes of the heap.

-Brandan

I laid out the logic for the page_nalloc function. The idea is that we iterate over the page numbers that are possible searching for a contiguous set of n pages that are free. If we can find this block, we set the taken bits for all pages to 1 and the last bit to 1 for the last page in the sequence. Return the page address of this using the known heap start and page size. Otherwise, return NULL in the case that n pages cannot be allocated. This has yet to be tested, but I will test it once the bookkeeping functions are implemented.

-Rob

## September 1, 2023
Based on lecture, I was able to reorganize the skeleton code based on that, and things made more sense. I'm going to stop taking all of the work now.

-Brandan

## August 31, 2023
Over the last few days, I discussed my thoughts with Andrew and Jay. We considered ways to implement the utility functions like `map_page_to_bk()` such as returning a byte and an offset or returning a bit index.

After some more thinking, I decided to go with the bit index as it makes the other bookkeeping bit functions more useful and synergize with `map_page_to_bk()`, which does the heavy lifting (instead of taking a page address and mapping it directly to the bits to test/set/clear them in each of the bookkeeping bit functions, we can just do only the shifting calculations within each and provide indices returned from `map_page_to_bk()`).

I was curious about how accurate the page number calculations were, and I talked with Andrew about it. We discovered that 8192 bytes (2 pages) are being used for bookkeeping, so there are only 4 "wasted" bits (the overhead), which is especially negligible since the padding takes up more. However, it seems we do need to take into account where the real bookkeeping bits stop so that we don't map bits to a page past the end of the heap.

-Brandan

## August 29, 2023
I started laying out code for the paging lab, primarily proposed prototypes of the static utility functions for bookkeeping byte management. I also began implementation of `page_init()`.

So far, I have learned that `grep` is helpful to find definitions of things from the template like `sym_start()` or `ALIGN_UP_POT()`--not sure if this is the best way to do it though.

I'm currently stuck thinking about the cleanest way to implement the bookkeeping functions without writing so many of them (test/set/clear for each of the 2 bits). The mapping function is also an interesting challenge. I will discuss with my team this week.

-Brandan

---
---

Every week, you will need to write entries in this journal. Include brief information about:

* any complications you've encountered.
* any interesting information you've learned.
* any complications that you've fixed and how you did it.

Sort your entries in descending order (newest entries at the top).

## 11-May-2022

EXAMPLE

## 09-May-2022

EXAMPLE

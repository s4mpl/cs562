import subprocess
import signal
import time

c_failed = 0
num_runs = 1000
for i in range(num_runs):
    print("starting proc ", i, "/", num_runs);
    input_to_send = b"\x01\x78"
    proc = subprocess.Popen(["make", "run"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.5)
    out, err = proc.communicate(input=input_to_send)
    search_strs = ["current_key: poem.linked.txt", "current_key: errno.c"]
    # search_strs = ["current_key: root.txt", "current_key: book1.txt"]
    search_section = str(out.decode())
    # print(search_section)
    # if search_for not in search_section or search_for2 not in search_section:
    if not all(search_str in search_section for search_str in search_strs):
        c_failed += 1
        print("num failed ", c_failed)
        print("last section... ", search_section)
        
print("num failed ", c_failed)
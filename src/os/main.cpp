#include "util/string.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "util/types.h"
#include "util/util.h"
#include "filesystem/filesystem.h"
#include "drivers/ata.h"

#include "interpreter/interpreter.h"

extern "C" void main();
extern "C" void handleCommands();
extern "C" void __main() {}

#ifndef FS_FORMAT_BLOCKS
#define FS_FORMAT_BLOCKS 0u
#endif
#ifndef FS_FORMAT_INODES
#define FS_FORMAT_INODES 64u
#endif

bool exited = true;

static inline void outw(u16 port, u16 val) {
    asm volatile ("outw %0, %1" : : "a"(val), "d"(port));
}

static inline void poweroff() {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    while (1) {
        asm volatile ("hlt");
    }
}

static void print_size_auto(const char* label, u64 bytes) {
    char buf[20];
    if (bytes >= (1ull << 30)) {
        u64 val = (bytes + (1ull << 29)) >> 30;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        println(" GB");
    } else if (bytes >= (1ull << 20)) {
        u64 val = (bytes + (1ull << 19)) >> 20;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        println(" MB");
    } else {
        u64 val = (bytes + 1023ull) >> 10;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        println(" KB");
    }
}

static void print_size_autonl(const char* label, u64 bytes) {
    char buf[20];
    if (bytes >= (1ull << 30)) {
        u64 val = (bytes + (1ull << 29)) >> 30;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        print(" GB");
    } else if (bytes >= (1ull << 20)) {
        u64 val = (bytes + (1ull << 19)) >> 20;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        print(" MB");
    } else {
        u64 val = (bytes + 1023ull) >> 10;
        int_to_str((int)val, buf);
        print(label);
        print(buf);
        print(" KB");
    }
}

static inline void cpuid(u32 eax_in, u32* eax, u32* ebx, u32* ecx, u32* edx) {
    asm volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(eax_in)
    );
}

static inline bool get_cpu_brand(char out[49]) {
    u32 a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a < 0x80000004) {
        out[0] = '\0';
        return false;
    }

    u32* p = (u32*)out;
    cpuid(0x80000002, &a, &b, &c, &d); *p++ = a; *p++ = b; *p++ = c; *p++ = d;
    cpuid(0x80000003, &a, &b, &c, &d); *p++ = a; *p++ = b; *p++ = c; *p++ = d;
    cpuid(0x80000004, &a, &b, &c, &d); *p++ = a; *p++ = b; *p++ = c; *p++ = d;

    out[48] = '\0';
    return true;
}

extern "C" bool fs_init();

static const char* k_commands[] = {
    "help",
    "clear",
    "info",
    "print",
    "shutdown",
    "exit",
    "ls",
    "mkfs",
    "touch",
    "mkdir",
    "passwd",
    "pwd",
    "read",
    "write",
    "mv",
    "cp",
    "rm",
    "cd"
};

static char k_root[64] = "7:/";

static void resolve_fs_path(const char* input, char* out, u32 max_len) {
    char cwd[64];
    u32 cwd_len = 0;
    u32 k = 0;
    while (k_root[k] && k_root[k] != '/') k++;
    if (k_root[k] == '/') k++;
    while (k_root[k] && cwd_len < 63) cwd[cwd_len++] = k_root[k++];
    cwd[cwd_len] = '\0';

    const char* inp = input;
    while (inp[0] == '.' && inp[1] == '.' && (inp[2] == '/' || inp[2] == '\0')) {
        if (cwd_len > 0) {
            int last_sep = -1;
            for (u32 i = 0; i < cwd_len; i++) {
                if (cwd[i] == '/') last_sep = (int)i;
            }
            cwd_len = (last_sep >= 0) ? (u32)last_sep : 0;
            cwd[cwd_len] = '\0';
        }
        if (inp[2] == '/') inp += 3;
        else { inp += 2; break; }
    }

    u32 p = 0;
    bool inp_has_sep = false;
    for (u32 i = 0; inp[i]; i++) {
        if (inp[i] == '/') { inp_has_sep = true; break; }
    }
    if (!inp_has_sep && cwd_len > 0) {
        for (u32 i = 0; i < cwd_len && p < max_len - 1; i++) out[p++] = cwd[i];
        if (inp[0] && p < max_len - 1) out[p++] = '/';
    }
    for (u32 i = 0; inp[i] && p < max_len - 1; i++) out[p++] = inp[i];
    out[p] = '\0';
}

extern "C" void init() {
    println("fs_init...");
    if (!fs_init()) {
        println("fs_format...");
        fs_format(FS_FORMAT_BLOCKS, FS_FORMAT_INODES);
        println("fs_init2...");
        fs_init();
    }
    println("fs done");

    while (1) {
        if (exited) {
            clear_screen();
            int attempts = 0;
            while (attempts < 3) {
                print("Scrivi la password: ");
                String text = readl();
                u32 input_hash = fs_hash_password(text.c_str());
                u32 stored_hash = 0;
                if (!fs_read_passwd(&stored_hash)) {
                    stored_hash = fs_hash_password("password123");
                }
                if (input_hash == stored_hash) {
                    clear_screen();
                    exited = false;
                    break;
                } else {
                    println("Accesso negato!");
                }
                attempts++;
            }
            if (exited) {
                println("Troppi tentativi, sistema bloccato.");
                poweroff();
                continue;
            }
        }

        main();
    }
}

extern "C" void main() {
    println("Accesso consentito!");
    wait(3);
    clear_screen();
    println("Benvenuto in 7OS!");
    println("Digita help per vedere i comandi disponibili.");
    handleCommands();
}

extern "C" void handleCommands() {
    while (!exited) {
        print(k_root);
        print("> ");
        String command = readl();
        ArrayList<String> parts = command.split(' ');
        String* cmd = (parts.size() > 0) ? &parts[0] : 0;
        bool matched = false;

        if (cmd && cmd->equals("help")) {
            println("Available commands:");
            for (u32 i = 0; i < (sizeof(k_commands) / sizeof(k_commands[0])); i++) {
                println(k_commands[i]);
            }
            matched = true;
        } else if(cmd && cmd->equals("shutdown")) {
            println("Shutting down...");
            poweroff();
        } else if (cmd && cmd->equals("clear")) {
            clear_screen();
            matched = true;
        } else if (cmd && cmd->equals("print")) {
            println("This is a test print command.");
            matched = true;
        } else if (cmd && cmd->equals("ls")) {
            DirEntry entries[16];
            u32 count = 0;
            bool ok;
            if (parts.size() > 1) {
                ok = fs_list_dir(parts[1].c_str(), entries, 16, &count);
            } else {
                u32 sep = 0;
                while (k_root[sep] && k_root[sep] != '/') sep++;
                if (k_root[sep] == '/' && k_root[sep + 1]) {
                    ok = fs_list_dir(&k_root[sep + 1], entries, 16, &count);
                } else {
                    ok = fs_list_root(entries, 16, &count);
                }
            }
            if (ok) {
                for (u32 i = 0; i < count; i++) {
                    println(entries[i].name);
                }
            } else {
                println("ls: directory non trovata o errore.");
            }
            matched = true;
        } else if (cmd && cmd->equals("mkfs")) {
            println("Formatting filesystem...");
            if (fs_format(FS_FORMAT_BLOCKS, FS_FORMAT_INODES) && fs_init()) {
                println("FS ready");
            } else {
                println("FS format failed");
            }
            matched = true;
        } else if(cmd && cmd->equals("info")) {
            println("7OS is a simple hobby operating system written in C++.");
            char brand[49];
            if (get_cpu_brand(brand)) {
                print("CPU NAME: ");
                println(brand);
                print_size_auto("RAM: ", get_ram_bytes());
                u64 fs_total = 0;
                u64 fs_used = 0;
                u64 fs_free = 0;
                u64 disk_sectors = 0;
                if (fs_get_usage(&fs_total, &fs_used, &fs_free)) {
                    print_size_autonl("Disk: ", fs_used);
                    print("/");
                    print_size_auto("", fs_free);
                }
            } else {
                println("CPU NAME: unknown");
            }
            matched = true;
        } else if(cmd && cmd->equals("exit")) {
            println("Exiting");
            exited = true;
            return;
        } else if (cmd && cmd->equals("touch")) {
            String filename;
            if (parts.size() > 1) {
                filename = String(parts[1].c_str());
            } else {
                println("Nome del file non specificato.");
            }
            if (parts.size() > 1) {
                const char* name = filename.c_str();
                bool has_sep = false;
                for (u32 i = 0; name[i]; i++) {
                    if (name[i] == '/') { has_sep = true; break; }
                }

                char filepath[64];
                u32 p = 0;
                if (!has_sep) {
                    u32 sep = 0;
                    while (k_root[sep] && k_root[sep] != '/') sep++;
                    if (k_root[sep] == '/' && k_root[sep + 1]) {
                        for (u32 i = sep + 1; k_root[i] && p < 63; i++) {
                            filepath[p++] = k_root[i];
                        }
                        if (p < 63) filepath[p++] = '/';
                    }
                }

                for (u32 i = 0; name[i] && p < 63; i++) filepath[p++] = name[i];
                filepath[p] = '\0';

                if (fs_write_path(filepath, "")) {
                    println("File creato con successo.");
                } else {
                    println("Creazione file fallita. Esiste gia' un file o una directory con lo stesso nome.");
                }
            }
            matched = true;
        } else if (cmd && cmd->equals("passwd")) {
            print("Password attuale: ");
            String old_pass = readl();
            u32 input_hash = fs_hash_password(old_pass.c_str());
            u32 stored_hash = 0;
            if (!fs_read_passwd(&stored_hash)) {
                stored_hash = fs_hash_password("password123");
            }
            if (input_hash != stored_hash) {
                println("Password errata.");
            } else {
                bool ok = false;
                do{
                    print("Nuova password: ");
                    String new_pass = readl();
                    print("Conferma nuova password: ");
                    String confirm_pass = readl();
                    if (!new_pass.equals(confirm_pass)) {
                        println("Le password non corrispondono.");
                    } else if (fs_write_passwd(fs_hash_password(new_pass.c_str()))) {
                        println("Password aggiornata.");
                        ok = true;
                    } else {
                        println("Errore aggiornamento password.");
                    }
                }while(!ok);
            }
            matched = true;
        } else if (cmd && cmd->equals("pwd")) {
            println(k_root);
            matched = true;
        }else if(cmd && cmd->equals("mkdir")) {
            if(parts.size() < 2) {
                println("Nome della directory non specificato.");
            } else {
                char filepath[64];
                resolve_fs_path(parts[1].c_str(), filepath, 64);
                if (filepath[0] == '\0' || !fs_mkdir_path(filepath)) {
                    println("Creazione directory fallita. Esiste gia' un file o una directory con lo stesso nome.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("write")) {
            if(parts.size() < 3) {
                println("Uso: write <filename> <data>");
            } else {
                String data;
                for (u32 i = 2; i < parts.size(); i++) {
                    if (i > 2) data.append(' ');
                    const char* word = parts[i].c_str();
                    for (u32 j = 0; word[j]; j++) data.append(word[j]);
                }
                char filepath[64];
                resolve_fs_path(parts[1].c_str(), filepath, 64);
                if (fs_write_path(filepath, data.c_str())) {
                    println("File scritto con successo.");
                } else {
                    println("Scrittura file fallita.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("read")) {
            if(parts.size() < 2) {
                println("Uso: read <filename>");
            } else {
                char filepath[64];
                resolve_fs_path(parts[1].c_str(), filepath, 64);
                char buffer[513];
                u32 len = 0;
                if (fs_read_path(filepath, buffer, 512, &len)) {
                    buffer[len] = '\0';
                    println(buffer);
                } else {
                    println("Lettura file fallita.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("cd")) {
            if(parts.size() < 2) {
                println("Uso: cd <directory>");
            } else {
                char fs_path[64];
                resolve_fs_path(parts[1].c_str(), fs_path, 64);
                if (fs_path[0] == '\0' || fs_is_dir(fs_path)) {
                    u32 p = 0;
                    k_root[p++] = '7'; k_root[p++] = ':'; k_root[p++] = '/';
                    for (u32 i = 0; fs_path[i] && p < 63; i++) k_root[p++] = fs_path[i];
                    k_root[p] = '\0';
                } else {
                    println("Directory non trovata.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("rm")) {
            if(parts.size() < 2) {
                println("Uso: rm [-r] <file/dir> [file/dir ...]");
            } else {
                bool recursive = false;
                bool any_target = false;
                bool all_ok = true;

                for (u32 a = 1; a < parts.size(); a++) {
                    String arg = String(parts[a].c_str());
                    if (arg.equals("-r") || arg.equals("-R")) {
                        recursive = true;
                        continue;
                    }
                    any_target = true;
                    char filepath[64];
                    resolve_fs_path(arg.c_str(), filepath, 64);
                    bool ok = recursive ? fs_remove_path_recursive(filepath) : fs_remove_path(filepath);
                    if (!ok) {
                        all_ok = false;
                        print("Rimozione fallita: ");
                        println(arg.c_str());
                    }
                }

                if (!any_target) {
                    println("Uso: rm [-r] <file/dir> [file/dir ...]");
                } else if (all_ok) {
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("mv")) {
            if(parts.size() < 3) {
                println("Uso: mv <source> <destination>");
            } else {
                char src_path[64];
                char dst_path[64];
                resolve_fs_path(parts[1].c_str(), src_path, 64);
                resolve_fs_path(parts[2].c_str(), dst_path, 64);
                if (fs_rename(src_path, dst_path)) {
                } else {
                    println("mv fallito.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("cp")) {
            if(parts.size() < 3) {
                println("Uso: cp <source> <destination>");
            } else {
                char src_path[64];
                char dst_path[64];
                resolve_fs_path(parts[1].c_str(), src_path, 64);
                resolve_fs_path(parts[2].c_str(), dst_path, 64);
                if (fs_copy_path(src_path, dst_path)) {
                } else {
                    println("cp fallita.");
                }
            }
            matched = true;
        }else if(cmd && cmd->equals("test")) {
            test();
            matched = true;
        }

        if (!matched && cmd && cmd->length() > 0) {
            println("Comando sconosciuto: digita help per vedere i comandi disponibili.");
        }
    }
}
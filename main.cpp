#include <iostream>
#include <map>
#include <vector>
#include <stdint.h>
#include <stdint-gcc.h>

using namespace std;

typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;

typedef struct _IMAGE_DATA_DIRECTORY {
    DWORD rva;
    DWORD size;
} IMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_SECTION_HEADER {
    BYTE    name[8];
    union {
        DWORD physicalAddress;
        DWORD virtualSize;
    } misc;
    DWORD   virtualAddress;
    DWORD   sizeOfRawData;
    DWORD   pointerToRawData;
    DWORD   pointerToRelocations;
    DWORD   pointerToLinenumbers;
    WORD    numberOfRelocations;
    WORD    numberOfLinenumbers;
    DWORD   characteristics;
} IMAGE_SECTION_HEADER;

typedef struct _IMAGE_EXPORT_DIRECTORY {
    DWORD   characteristics;
    DWORD   timeDateStamp;
    WORD    majorVersion;
    WORD    minorVersion;
    DWORD   name;
    DWORD   base;
    DWORD   numberOfFunctions;
    DWORD   numberOfNames;
    DWORD   addressOfFunctions;
    DWORD   addressOfNames;
    DWORD   addressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY;

#define IMAGE_DIRECTORY_ENTRY_EXPORT    0   // Export Directory

map<string, pair<int, DWORD>> addressByName;

unsigned int imageBase;
unsigned int numberOfSections;
unsigned int sectionAlignment = 0x50;

IMAGE_SECTION_HEADER *sections;

#define ALIGN_DOWN(x, align)  ( x & ~(align-1))
#define ALIGN_UP(x, align)    ((x &  (align-1)) ? ALIGN_DOWN(x,align)+align : x)

int defSection(DWORD rva) {

    for (int i = 0; i < numberOfSections; ++i) {
        DWORD start = sections[i].virtualAddress;
        DWORD end = start + ALIGN_UP(sections[i].misc.virtualSize, sectionAlignment);

        if(rva >= start && rva < end)
            return i;
    }
    return -1;
}

DWORD rva2raw(DWORD rva)  {
    //Image Base = app's entry point in virtual memory

    //VA = ImageBase + RVA;
    //RAW = RVA - sectionRVA + rawSection;
    // rawSection - смещение до секции от начала файла
    // sectionRVA - RVA секции (это поле хранится внутри секции)

    //RVA >= sectionVitualAddress && RVA < ALIGN_UP(sectionVirtualSize, sectionAligment)
    // sectionAligment - выравнивание для секции. Значение можно узнать в Optional-header.
    // sectionVitualAddress - RVA секции - хранится непосредственно в секции
    // ALIGN_UP() - функция, определяющая сколько занимает секция в памяти, учитывая выравнивание

    int indexSection = defSection(rva);
    if(indexSection != -1)
        return rva - sections[indexSection].virtualAddress + sections[indexSection].pointerToRawData;
    else
        return 0;
}

IMAGE_SECTION_HEADER parseSection(FILE *f, int ptr) {

    IMAGE_SECTION_HEADER section;

    fseek(f, ptr, SEEK_SET);

    fread(&section.name,                   8, 1, f);
    fread(&section.misc.virtualSize,       4, 1, f);
    fread(&section.virtualAddress,         4, 1, f);
    fread(&section.sizeOfRawData,          4, 1, f);
    fread(&section.pointerToRawData,       4, 1, f);
    fread(&section.pointerToRelocations,   4, 1, f);
    fread(&section.pointerToLinenumbers,   4, 1, f);
    fread(&section.numberOfRelocations,    2, 1, f);
    fread(&section.numberOfLinenumbers,    2, 1, f);
    fread(&section.characteristics,        4, 1, f);

    cout << section.name << "\t\t"
        << hex << section.misc.virtualSize << "\t\t"
        << hex << section.virtualAddress << "\t\t"
        << hex << section.pointerToRawData << endl;

    return section;
}

string getName(FILE *f, int ptr) {
    BYTE tmp = 1;
    string funcName = "";

    fseek(f, ptr, SEEK_SET);

    while (0 != tmp) {

        fread(&tmp, 1, 1, f);
        funcName += tmp;
    }

    return funcName.substr(0, funcName.length() - 1);//remove space
}

void parseExportTable(FILE *f, int ptr) {
    DWORD tmp = 0;
    IMAGE_EXPORT_DIRECTORY exportDir;

    fseek(f, ptr, SEEK_SET);

    fread(&exportDir.characteristics,       4, 1, f);
    fread(&exportDir.timeDateStamp,         4, 1, f);
    fread(&exportDir.majorVersion,          2, 1, f);
    fread(&exportDir.minorVersion,          2, 1, f);
    fread(&exportDir.name,                  4, 1, f);
    fread(&exportDir.base,                  4, 1, f);
    fread(&exportDir.numberOfFunctions,     4, 1, f);
    fread(&exportDir.numberOfNames,         4, 1, f);
    fread(&exportDir.addressOfFunctions,    4, 1, f);
    fread(&exportDir.addressOfNames,        4, 1, f);
    fread(&exportDir.addressOfNameOrdinals, 4, 1, f);

    cout << "num of Names: #" << dec << exportDir.numberOfNames << endl;
    cout << "num of Funcs: #" << dec << exportDir.numberOfFunctions << endl;


    auto ptrToAddr = rva2raw(exportDir.addressOfFunctions);
    auto ptrToNames = rva2raw(exportDir.addressOfNames);
    auto ptrToOrdinals = rva2raw(exportDir.addressOfNameOrdinals);

    vector<string> names;
    vector<int> ordinals;
    vector<DWORD> addresses;

    for (int i = 0; i < exportDir.numberOfNames; i++) {

        fseek(f, ptrToNames, SEEK_SET);
        fread(&tmp, 4, 1, f);
        string name = getName(f, tmp);

        tmp = 0;
        fseek(f, ptrToOrdinals, SEEK_SET);
        fread(&tmp, 2, 1, f);
        WORD ordinal = (WORD) tmp;

        names.push_back(name);
        ordinals.push_back(ordinal);

        ptrToNames += 0x4;
        ptrToOrdinals += 0x2;
    }


    for (int i = 0; i < exportDir.numberOfFunctions; i++) {

        fseek(f, ptrToAddr, SEEK_SET);
        fread(&tmp, 4, 1, f);

        addresses.push_back(tmp);

        ptrToAddr += 0x4;
    }



    for (int i = 0; i != exportDir.numberOfNames &&
            i != exportDir.numberOfFunctions; i++)

        addressByName.insert(
                pair<string, pair<int, DWORD>>
                        (names[i], pair<int, DWORD>(ordinals[i], addresses[i])));

}

void outputAllFunctions() {
    cout << "Functions (ordinal / address / name):" << endl;
    for (auto name : addressByName) {
        cout << dec << name.second.first << "\t"
            << hex << name.second.second << "\t"
            << name.first << endl;
    }
}

void searchByName(string name) {
    cout << endl << "Search address by name '" << name << "': ";

    if (addressByName.end() == addressByName.find(name))
        cout << "no such function";
    else
        cout << addressByName.find(name)->second.second;

    cout << endl;
}

bool parsePE(FILE *f) {
    int ptr;

    if (f) {
        if ('M' == fgetc(f) && 'Z' == fgetc(f)) {

            rewind(f);
            unsigned int c = 1;
            fseek(f, 0, SEEK_SET);
            fread(&c, 2, 1, f);
            cout << "e_magic:\t\t\t\t0x" << hex << c << endl;

            unsigned int e_lfnew = 1;
            fseek(f, 0x3C, SEEK_SET);
            fread(&e_lfnew, 4, 1, f);
            cout << "e_lfnew:\t\t\t\t0x" << hex << e_lfnew << endl;

            unsigned int peHeader = 1;
            fseek(f, e_lfnew, SEEK_SET);
            fread(&peHeader, 4, 1, f);
            cout << "peHeader:\t\t\t\t0x" << hex << peHeader << endl;

            fseek(f, e_lfnew + 0x30 + 4, SEEK_SET);
            fread(&imageBase, 4, 1, f);
            cout << "imageBase:\t\t\t\t0x" << hex << imageBase << endl;

            fseek(f, e_lfnew + 0x6, SEEK_SET);
            fread(&numberOfSections, 2, 1, f);
            cout << "numberOfSections:\t\t#" << dec << numberOfSections << endl;

            unsigned int numberOfRvaAndSizes = 1;
            fseek(f, e_lfnew + 0x70 + 4, SEEK_SET);
            fread(&numberOfRvaAndSizes, 4, 1, f);
            cout << "numberOfRvaAndSizes:\t#" << dec << numberOfRvaAndSizes << endl;



            if (0 != numberOfRvaAndSizes) {


                IMAGE_DATA_DIRECTORY directories[numberOfRvaAndSizes];

                ptr = e_lfnew + 0x70 + 0x8;
                for (int i = 0; i < numberOfRvaAndSizes; i++) {

                    fseek(f, ptr, SEEK_SET);
                    fread(&directories[i].rva, 4, 1, f);
                    fread(&directories[i].size, 4, 1, f);

                    ptr += 0x8;
                }


                sections = new IMAGE_SECTION_HEADER[numberOfSections];

                cout << endl << "SECTIONS (name / size / rva / raw):" << endl;
                for (int i = 0; i < numberOfSections; i++) {

                    sections[i] = parseSection(f, ptr);
                    ptr += sizeof(IMAGE_SECTION_HEADER); //0x28
                }

                cout << endl <<"DATA_DIRECTORIES (size / rva -> raw):" << endl;
                for (int i = 0; i < numberOfRvaAndSizes; i++) {

                    DWORD resolvedRva = rva2raw(directories[i].rva);

                    cout << hex << directories[i].size << "\t\t"
                    << hex << directories[i].rva << " -> "
                    << hex << resolvedRva << endl;
                }



                if (0 != directories[IMAGE_DIRECTORY_ENTRY_EXPORT].rva &&
                    0 != directories[IMAGE_DIRECTORY_ENTRY_EXPORT].size) {

                    cout << endl << "EXPORT_TABLE (rva / raw):" << endl;

                    DWORD rva = directories[IMAGE_DIRECTORY_ENTRY_EXPORT].rva;
                    DWORD raw = rva2raw(rva);

                    cout << hex << rva << "\t\t" << hex << raw << endl;

                    parseExportTable(f, raw);

                    return true;

                } else {
                    cout << "empty export table";
                }

            } else {
                cout << "data directories empty. file corrupted ?";
            }

        } else {
            cout << "file is not of PE-format";
        }

        fclose(f);
    } else {
        cout << "no such file";
    }

    return false;
}

int main() {

    FILE *f = fopen(
            //"C:\\Users\\anthony\\Documents\\Вирусы\\Lab03-03\\Lab03-03.exe",
            "/home/anthony/Dropbox/Steam.dll",
            //"/home/anthony/Рабочий стол/twain_32.dll",
            "rb");

    cout << "Kacnep napcep" << endl;

    if (parsePE(f)) {

        outputAllFunctions();

        searchByName("SteamUnsubscribe");
    }

    //getchar();
    return 0;
}
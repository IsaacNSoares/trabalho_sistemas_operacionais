/*
*  myfs.c - Implementacao do sistema de arquivos MyFS
*
*  Autores: Eduarda Pereira Mourão Nunes - 202376015
*           Isaac Nascimento Soares - 202376018
*           Vitor Fernandes Gomes - 202365146AC
*
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include <stdio.h>
#include <stdlib.h>
#include "myfs.h"
#include "vfs.h"
#include "inode.h"
#include "util.h"
#include "string.h"

//Declaracoes globais
#define MYFS_ID 'M' // Identificador do MyFS
#define SECTOR_FREE_BLOCK_MAP 1 // Setor para o índice do próximo bloco livre
#define FIRST_DATA_BLOCK 100 // Setor onde começam os dados
#define ROOT_INODE 1

//Estrutura para entrada de diretório
typedef struct {
	unsigned int inode;
	char name[MAX_FILENAME_LENGTH + 1];
} DirEntry;

//Estrutura interna pra gerenciar arquivos abertos
typedef struct {
	int used;						    // 0 = livre | 1 = sendo usado
	unsigned int inodeNum;	// Numero do Inode associado
	unsigned int cursor;		// Posicao atual do cursor no arquivo(em bytes)
	Disk *d;								// Disco em que o arquivo está
} MyFileHandle;

MyFileHandle openFiles[MAX_FDS];  //Tabela de arquivos abertos

// Retorna o próximo setor livre e atualiza o contador do disco
unsigned int __allocBlock(Disk *d) {

	unsigned char buffer[DISK_SECTORDATASIZE];
	unsigned int nextFree;

	if(diskReadSector(d, SECTOR_FREE_BLOCK_MAP, buffer) < 0){
		return 0;
	}

	char2ul(buffer, &nextFree);

   	// Verifica se o disco encheu
	if(nextFree >= diskGetNumSectors(d)){
		return 0;
	}

   	// Atualiza o próximo livre
	unsigned int newNextFree = nextFree + 1;
	ul2char(newNextFree, buffer);
	diskWriteSector(d, SECTOR_FREE_BLOCK_MAP, buffer);

	return nextFree;
}

// Busca um inode pelo nome DIRETO NA RAIZ
// Retorna o numero do inode se achar, ou 0 se não achar
unsigned int __findInodeInRoot(Disk *d, const char *name) {
    Inode *root = inodeLoad(ROOT_INODE, d);
    if (!root) return 0;

    unsigned char buffer[DISK_SECTORDATASIZE];
    DirEntry *entry;
    unsigned int entriesPerSector = DISK_SECTORDATASIZE / sizeof(DirEntry);

    unsigned int numBlocks = inodeGetFileSize(root) / DISK_SECTORDATASIZE;
    if (inodeGetFileSize(root) % DISK_SECTORDATASIZE != 0) {
        numBlocks++;
    }

    for (unsigned int i = 0; i < numBlocks; i++) {
        unsigned int blockAddr = inodeGetBlockAddr(root, i);
        if (blockAddr == 0) continue;

        if (diskReadSector(d, blockAddr, buffer) == 0) {
            entry = (DirEntry *)buffer;

            for (unsigned int j = 0; j < entriesPerSector; j++) {
                if (entry[j].inode != 0 && strcmp(entry[j].name, name) == 0) {
                    unsigned int foundInode = entry[j].inode;
                    free(root);
                    return foundInode;
                }
            }
        }
    }

    free(root);
    return 0;
}


//Funcao para verificacao se o sistema de arquivos está ocioso, ou seja,
//se nao ha quisquer descritores de arquivos em uso atualmente. Retorna
//um positivo se ocioso ou, caso contrario, 0.
int myFSIsIdle (Disk *d) {

	for(int i = 0; i<MAX_FDS; i++){
		if(openFiles[i].used && openFiles[i].d == d)
			return 0;
	}

	return 1;
}

//Funcao para formatacao de um disco com o novo sistema de arquivos
//com tamanho de blocos igual a blockSize. Retorna o numero total de
//blocos disponiveis no disco, se formatado com sucesso. Caso contrario,
//retorna -1.
int myFSFormat (Disk *d, unsigned int blockSize) {
	// Validação: blockSize deve ser múltiplo do tamanho do setor
	if (blockSize == 0 || blockSize % DISK_SECTORDATASIZE != 0) {
		return -1;
	}

	unsigned char buffer[DISK_SECTORDATASIZE];
	unsigned int firstDataBlock = FIRST_DATA_BLOCK;

	memset(buffer, 0, DISK_SECTORDATASIZE);

	// Grava o primeiro bloco de dados livre
	ul2char(firstDataBlock, buffer);
	if(diskWriteSector(d, SECTOR_FREE_BLOCK_MAP, buffer) < 0){
		return -1;
	}

	// Cria o inode raiz
	Inode *root = inodeCreate(ROOT_INODE, d);
	if(!root){
		return -1;
	}

	inodeSetFileType(root, FILETYPE_DIR);
	inodeSetFileSize(root, 0);
	inodeSetOwner(root, 0);
	inodeSave(root);
	free(root);

	// Inicializa todos os inodes disponíveis
	unsigned int inodesPerSector = inodeNumInodesPerSector();
	unsigned int inodeAreaSectors = FIRST_DATA_BLOCK - inodeAreaBeginSector();
	unsigned int numInodesToInit = inodesPerSector * inodeAreaSectors;
	for(unsigned int i = 2; i <= numInodesToInit; i++){
		Inode *inode = inodeCreate(i, d);
		if(!inode) continue;
		free(inode);
	}
	
	// Calcula o número total de blocos disponíveis considerando blockSize
	unsigned int sectorsPerBlock = blockSize / DISK_SECTORDATASIZE;
	unsigned int totalSectors = diskGetNumSectors(d) - firstDataBlock;
	unsigned int totalBlocks = totalSectors / sectorsPerBlock;
	
	return totalBlocks;
}

// Função auxiliar para encontrar slot livre
int __findFreeSlot(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!openFiles[i].used) {
            return i;
        }
    }
    return -1;
}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount(Disk *d, int x) {
    if (!d) return 0;

    if (x == 1) { // Montagem
        // Inicializa tabela de arquivos abertos
        for (int i = 0; i < MAX_FDS; i++) {
            openFiles[i].used = 0;
        }
        return 1;
    }

    if (x == 0) { // Desmontagem
        return 1;
    }

    return 0;
}

// Retorna 0 em sucesso, -1 em erro
int __extractFileName(const char *path, char *outName) {
    const char *p = path;
    while (*p == '/') p++;
    
    if (*p == '\0') return -1;      // Caminho vazio
    if (strchr(p, '/')) return -1;  // Subdiretórios não suportados

    strncpy(outName, p, MAX_FILENAME_LENGTH);
    outName[MAX_FILENAME_LENGTH] = '\0';
    return 0;
}

// Adiciona entrada DIRETO NA RAIZ
// Retorna 0 em sucesso, -1 em erro
int __addEntryToRoot(Disk *d, unsigned int newInodeNum, const char *name) {
    Inode *root = inodeLoad(ROOT_INODE, d);
    if (!root) return -1;

    unsigned char sector[DISK_SECTORDATASIZE];
    int entriesPerSector = DISK_SECTORDATASIZE / sizeof(DirEntry);
    
    // Tenta achar um buraco em blocos existentes
    for (unsigned int b = 0; ; b++) {
        unsigned int blockAddr = inodeGetBlockAddr(root, b);
        if (blockAddr == 0) break;

        if (diskReadSector(d, blockAddr, sector) < 0) continue;
        DirEntry *entries = (DirEntry *)sector;

        for (int i = 0; i < entriesPerSector; i++) {
            if (entries[i].inode == 0) {
                entries[i].inode = newInodeNum;
                strncpy(entries[i].name, name, MAX_FILENAME_LENGTH);
                entries[i].name[MAX_FILENAME_LENGTH] = '\0';
                
                if (diskWriteSector(d, blockAddr, sector) < 0) {
                    free(root);
                    return -1;
                }
                
                unsigned int endPos = b * DISK_SECTORDATASIZE + (i + 1) * sizeof(DirEntry);
                if (inodeGetFileSize(root) < endPos) {
                    inodeSetFileSize(root, endPos);
                    inodeSave(root);
                }
                free(root);
                return 0;
            }
        }
    }

    // Precisa alocar novo bloco
    unsigned int newBlock = __allocBlock(d);
    if (newBlock == 0) {
        free(root);
        return -1;
    }

    if (inodeAddBlock(root, newBlock) < 0) {
        free(root);
        return -1;
    }

    memset(sector, 0, DISK_SECTORDATASIZE);
    DirEntry *entries = (DirEntry *)sector;
    
    entries[0].inode = newInodeNum;
    strncpy(entries[0].name, name, MAX_FILENAME_LENGTH);
    entries[0].name[MAX_FILENAME_LENGTH] = '\0';

    if (diskWriteSector(d, newBlock, sector) < 0) {
        free(root);
        return -1;
    }

    unsigned int numBlocks = 0;
    while(inodeGetBlockAddr(root, numBlocks) != 0) numBlocks++;
    
    unsigned int newSize = (numBlocks - 1) * DISK_SECTORDATASIZE + sizeof(DirEntry);
    inodeSetFileSize(root, newSize);
    inodeSave(root);

    free(root);
    return 0;
}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen(Disk *d, const char *path) {
    if (!d || !path) return -1;
    if (path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) return -1;

    // Extrai o nome do arquivo (remove barras)
    char name[MAX_FILENAME_LENGTH + 1];
    if (__extractFileName(path, name) < 0) return -1;

    int slot = __findFreeSlot();
    if (slot < 0) return -1;

    // Procura arquivo DIRETO NA RAIZ
    unsigned int inodeNum = __findInodeInRoot(d, name);

    if (inodeNum > 0) {
        // Arquivo existe
        Inode *inode = inodeLoad(inodeNum, d);
        if (!inode) return -1;
        inodeSetRefCount(inode, inodeGetRefCount(inode) + 1);
        inodeSave(inode);
        free(inode);
    } else {
        // Cria novo arquivo
        inodeNum = inodeFindFreeInode(2, d);
        if (inodeNum == 0) return -1;

        Inode *newInode = inodeCreate(inodeNum, d);
        if (!newInode) return -1;
        inodeSetFileType(newInode, FILETYPE_REGULAR);
        inodeSetRefCount(newInode, 1);
        inodeSave(newInode);
        free(newInode);

        // Adiciona direto na raiz
        if (__addEntryToRoot(d, inodeNum, name) < 0) {
            return -1;
        }
    }

    openFiles[slot].used = 1;
    openFiles[slot].inodeNum = inodeNum;
    openFiles[slot].cursor = 0;
    openFiles[slot].d = d;

    return slot + 1;
}

//Funcao para a leitura de um arquivo, a partir de um descritor de arquivo
//existente. Os dados devem ser lidos a partir da posicao atual do cursor
//e copiados para buf. Terao tamanho maximo de nbytes. Ao fim, o cursor
//deve ter posicao atualizada para que a proxima operacao ocorra a partir
//do próximo byte apos o ultimo lido. Retorna o numero de bytes
//efetivamente lidos em caso de sucesso ou -1, caso contrario.

int myFSRead (int fd, char *buf, unsigned int nbytes) {

    int idx = fd - 1; // Ajuste do FD para o índice do array
    if (idx < 0 || idx >= MAX_FDS || !openFiles[idx].used) return -1;

    MyFileHandle *h = &openFiles[idx];
    Inode *inode = inodeLoad(h->inodeNum, h->d);
    if (!inode) return -1;

    unsigned int fileSize = inodeGetFileSize(inode);
    if (h->cursor >= fileSize) {
        free(inode);
        return 0; // Fim do arquivo
    }

    // Não lê além do tamanho do arquivo
    if (h->cursor + nbytes > fileSize) {
        nbytes = fileSize - h->cursor;
    }

    unsigned int bytesRead = 0;
    unsigned char sectorBuffer[DISK_SECTORDATASIZE];

    while (bytesRead < nbytes) {
        unsigned int logicalBlock = (h->cursor) / DISK_SECTORDATASIZE;
        unsigned int offsetInBlock = (h->cursor) % DISK_SECTORDATASIZE;
        unsigned int physSector = inodeGetBlockAddr(inode, logicalBlock);

        if (diskReadSector(h->d, physSector, sectorBuffer) < 0) break;

        unsigned int canRead = DISK_SECTORDATASIZE - offsetInBlock;
        unsigned int remaining = nbytes - bytesRead;
        unsigned int toCopy = (remaining < canRead) ? remaining : canRead;

        memcpy(buf + bytesRead, sectorBuffer + offsetInBlock, toCopy);

        bytesRead += toCopy;
        h->cursor += toCopy;
    }

    free(inode);
    return bytesRead;
}

//Funcao para a escrita de um arquivo, a partir de um descritor de arquivo
//existente. Os dados de buf sao copiados para o disco a partir da posição
//atual do cursor e terao tamanho maximo de nbytes. Ao fim, o cursor deve
//ter posicao atualizada para que a proxima operacao ocorra a partir do
//proximo byte apos o ultimo escrito. Retorna o numero de bytes
//efetivamente escritos em caso de sucesso ou -1, caso contrario

int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
    int idx = fd - 1;
    if (idx < 0 || idx >= MAX_FDS || !openFiles[idx].used) return -1;

    MyFileHandle *h = &openFiles[idx];
    Inode *inode = inodeLoad(h->inodeNum, h->d); //
    if (!inode) return -1;

    unsigned int bytesWritten = 0;
    unsigned char sectorBuffer[DISK_SECTORDATASIZE];

    while (bytesWritten < nbytes) {
        unsigned int logicalBlock = (h->cursor) / DISK_SECTORDATASIZE;
        unsigned int offsetInBlock = (h->cursor) % DISK_SECTORDATASIZE;
        
        // Tenta obter o endereço do bloco atual
        unsigned int physSector = inodeGetBlockAddr(inode, logicalBlock);

        // Se o bloco não existe, aloca e adiciona
        if (physSector == 0) {
            physSector = __allocBlock(h->d);
            if (physSector == 0) break; // Disco cheio
            
            if (inodeAddBlock(inode, physSector) == -1) {
                break;
            }
        }

        diskReadSector(h->d, physSector, sectorBuffer);

        unsigned int canWrite = DISK_SECTORDATASIZE - offsetInBlock;
        unsigned int remaining = nbytes - bytesWritten;
        unsigned int toCopy = (remaining < canWrite) ? remaining : canWrite;

        memcpy(sectorBuffer + offsetInBlock, buf + bytesWritten, toCopy);
        
        if (diskWriteSector(h->d, physSector, sectorBuffer) < 0) break;

        bytesWritten += toCopy;
        h->cursor += toCopy;
    }

    // Atualiza o tamanho do arquivo no i-node se ele cresceu
    if (h->cursor > inodeGetFileSize(inode)) {
        inodeSetFileSize(inode, h->cursor);
        inodeSave(inode); // Salva as alterações de tamanho
    }

    free(inode);
    return bytesWritten;
}

//Funcao para fechar um arquivo, a partir de um descritor de arquivo
//existente. Retorna 0 caso bem sucedido, ou -1 caso contrario
int myFSClose (int fd) {
    int idx = fd - 1; // Ajuste para o índice do array
    if (idx < 0 || idx >= MAX_FDS || !openFiles[idx].used) return -1;

    // Libera o slot na tabela de arquivos abertos
    openFiles[idx].used = 0;
    openFiles[idx].inodeNum = 0;
    openFiles[idx].cursor = 0;
    openFiles[idx].d = NULL;

    return 0;
}

//Funcao para instalar seu sistema de arquivos no S.O., registrando-o junto
//ao virtual FS (vfs). Retorna um identificador unico (slot), caso
//o sistema de arquivos tenha sido registrado com sucesso.
//Caso contrario, retorna -1
int installMyFS (void) {

	FSInfo *fsInfo = malloc(sizeof(FSInfo));
	fsInfo->fsid = MYFS_ID;
	fsInfo->fsname = "MyFS";
	fsInfo->isidleFn = myFSIsIdle;
	fsInfo->formatFn = myFSFormat;
	fsInfo->xMountFn = myFSxMount;
	fsInfo->openFn = myFSOpen;
	fsInfo->readFn = myFSRead;
	fsInfo->writeFn = myFSWrite;
	fsInfo->closeFn = myFSClose;

	return vfsRegisterFS(fsInfo);
}

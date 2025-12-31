/*
*  myfs.c - Implementacao do sistema de arquivos MyFS
*
*  Autores: Isaac Nascimento Soares - 202376018
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

// Busca um inode pelo nome dentro de um diretório pai
// Retorna o numero do inode se achar, ou 0 se não achar
unsigned int __findInodeInDir(Disk *d, unsigned int parentInodeNum, const char *name){

	Inode *parent = inodeLoad(parentInodeNum, d);
	if(!parent){
		return 0;
	}

	unsigned int numBlocks = inodeGetFileSize(parent) / DISK_SECTORDATASIZE;
	if(inodeGetFileSize(parent) % DISK_SECTORDATASIZE != 0){
		numBlocks++;
	}

	unsigned char buffer[DISK_SECTORDATASIZE];
	DirEntry *entry;

	for(unsigned int i = 0; i < numBlocks; i++){
		unsigned int blockAddr = inodeGetBlockAddr(parent, i);
		if(blockAddr == 0){
			continue;
		}

		if(diskReadSector(d, blockAddr, buffer) == 0){
			entry = (DirEntry *)buffer;
			if(entry->inode != 0 && strcmp(entry->name, name) == 0){
				free(parent);
				return entry->inode;
			}
		}

	}

	free(parent);
	return 0;
}

//Funcao para verificacao se o sistema de arquivos está ocioso, ou seja,
//se nao ha quisquer descritores de arquivos em uso atualmente. Retorna
//um positivo se ocioso ou, caso contrario, 0.
int myFSIsIdle (Disk *d) {

	for(int i = 0; i<MAX_FDS; i++){
		if(openFiles[i].used && openFiles[i].d == d);
		return 0;
	}

	return 1;
}

//Funcao para formatacao de um disco com o novo sistema de arquivos
//com tamanho de blocos igual a blockSize. Retorna o numero total de
//blocos disponiveis no disco, se formatado com sucesso. Caso contrario,
//retorna -1.
int myFSFormat (Disk *d, unsigned int blockSize) {
	
	//Inicializa o setor de mapa de bits (Next Free Block)
	unsigned char buffer[DISK_SECTORDATASIZE];
	unsigned int firstDataBlock = FIRST_DATA_BLOCK;

	// Limpa o buffer com zeros
	memset(buffer, 0, DISK_SECTORDATASIZE);

	ul2char(firstDataBlock, buffer);
	if(diskWriteSector(d, SECTOR_FREE_BLOCK_MAP, buffer) < 0){
		return -1;
	}

	// Cria o Inode raiz (Inode 1)
	Inode *root = inodeCreate(1, d);
	if(!root){
		return -1;
	}

	inodeSetFileType(root, FILETYPE_DIR);
	inodeSetFileSize(root, 0);
	inodeSetOwner(root, 0); // Usuário root
	inodeSave(root);
	free(root);
	
	// Retorna numero total de blocos (estimado)
	return diskGetNumSectors(d) - firstDataBlock;
}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount (Disk *d, int x) {
	return 0;
}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen (Disk *d, const char *path) {
	return -1;
}
	
//Funcao para a leitura de um arquivo, a partir de um descritor de arquivo
//existente. Os dados devem ser lidos a partir da posicao atual do cursor
//e copiados para buf. Terao tamanho maximo de nbytes. Ao fim, o cursor
//deve ter posicao atualizada para que a proxima operacao ocorra a partir
//do próximo byte apos o ultimo lido. Retorna o numero de bytes
//efetivamente lidos em caso de sucesso ou -1, caso contrario.
int myFSRead (int fd, char *buf, unsigned int nbytes) {
	return -1;
}

//Funcao para a escrita de um arquivo, a partir de um descritor de arquivo
//existente. Os dados de buf sao copiados para o disco a partir da posição
//atual do cursor e terao tamanho maximo de nbytes. Ao fim, o cursor deve
//ter posicao atualizada para que a proxima operacao ocorra a partir do
//proximo byte apos o ultimo escrito. Retorna o numero de bytes
//efetivamente escritos em caso de sucesso ou -1, caso contrario
int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
	return -1;
}

//Funcao para fechar um arquivo, a partir de um descritor de arquivo
//existente. Retorna 0 caso bem sucedido, ou -1 caso contrario
int myFSClose (int fd) {
	return -1;
}

//Funcao para abertura de um diretorio, a partir do caminho
//especificado em path, no disco indicado por d, no modo Read/Write,
//criando o diretorio se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpenDir (Disk *d, const char *path) {
	return -1;
}

//Funcao para a leitura de um diretorio, identificado por um descritor
//de arquivo existente. Os dados lidos correspondem a uma entrada de
//diretorio na posicao atual do cursor no diretorio. O nome da entrada
//e' copiado para filename, como uma string terminada em \0 (max 255+1).
//O numero do inode correspondente 'a entrada e' copiado para inumber.
//Retorna 1 se uma entrada foi lida, 0 se fim de diretorio ou -1 caso
//mal sucedido
int myFSReadDir (int fd, char *filename, unsigned int *inumber) {
	return -1;
}

//Funcao para adicionar uma entrada a um diretorio, identificado por um
//descritor de arquivo existente. A nova entrada tera' o nome indicado
//por filename e apontara' para o numero de i-node indicado por inumber.
//Retorna 0 caso bem sucedido, ou -1 caso contrario.
int myFSLink (int fd, const char *filename, unsigned int inumber) {
	return -1;
}

//Funcao para remover uma entrada existente em um diretorio, 
//identificado por um descritor de arquivo existente. A entrada e'
//identificada pelo nome indicado em filename. Retorna 0 caso bem
//sucedido, ou -1 caso contrario.
int myFSUnlink (int fd, const char *filename) {
	return -1;
}

//Funcao para fechar um diretorio, identificado por um descritor de
//arquivo existente. Retorna 0 caso bem sucedido, ou -1 caso contrario.	
int myFSCloseDir (int fd) {
	return -1;
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
	fsInfo->opendirFn = myFSOpenDir;
	fsInfo->readdirFn = myFSReadDir;
	fsInfo->linkFn = myFSLink;
	fsInfo->unlinkFn = myFSUnlink;
	fsInfo->closedirFn = myFSCloseDir;

	return vfsRegisterFS(fsInfo);
}

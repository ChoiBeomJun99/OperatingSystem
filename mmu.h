#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//비트 연산에 필요한 mask들 미리 정의
#define OFFSET_MASK 0x03
#define PFN_MASK 0xFC
#define PRESENT_MASK 0x01
#define ALLOC_MASK 0x05

//각 공간 할당할 변수들
unsigned char* ku_mmu_swap_space = NULL;
unsigned char* ku_mmu_pmem = NULL;

//free 리스트
unsigned char* pmem_free_list = NULL;
unsigned char* swap_free_list = NULL;

//각 공간의 사이즈
unsigned int ku_mmu_mem_size;
unsigned int ku_mmu_swap_size;

//pte 정의 내부에는 entry만 존재.
struct ku_pte{
	unsigned char entry;
};

//pte의 배열은 곧, pageTable 각 프로세스마다 할당 해줄 pageTable 변수 선언
struct ku_pte* ku_mmu_pageTable = NULL;

struct ku_mmu_pcb{ //프로세스 당 할당 받을 수 있는 page의 크기는 4byte이다.
	unsigned char pid;
	struct ku_pte* cr3;
};


struct ku_mmu_pte_info{ //스와핑에 사용 될 Node 역할을 할 것.

	unsigned char pid;
	struct ku_pte* cr3;
	unsigned char va;
	unsigned char index_in_pmem_free; // 0일때는 아무데도 할당 되지 않은 것
	unsigned char index_in_swap_free; // 0일때는 아무데도 할당 되지 않은 것

	struct ku_mmu_pte_info* next; //이 다음에 매핑 된 것을 가리킨다. FIFO 구현을 위한 것
};

struct FIFO_QUEUE{ //스와핑에 사용 될 Queue
	int count;
	struct ku_mmu_pte_info *front, *rear;
};

//FIFO 구현을 위한 Queue 선언
struct FIFO_QUEUE* Queue = NULL;
struct ku_mmu_pcb* exist_p_list = NULL; //이미 pcb가 존재하는 프로세스 관리 리스트


// 노드 역할을 할 ku_mmu_pte_info를 생성해주고 값을 초기화 해주는 함수
struct ku_mmu_pte_info* createInfo(char pid, struct ku_pte* cr3, char va, char index_in_pmem_free, char index_in_swap_free){

	struct ku_mmu_pte_info* temp = (struct ku_mmu_pte_info*)malloc(sizeof(struct ku_mmu_pte_info));
	temp->pid = pid;
	temp->cr3 = cr3;
	temp->va = va;
	temp->index_in_pmem_free = index_in_pmem_free;
	temp->index_in_swap_free = index_in_swap_free;
	temp->next = NULL;

	return temp;
};

//deQueue의 역할을 하는 메소드
void enQueue(struct ku_mmu_pte_info* ku_mmu_pte_info){

	Queue->count +=1;
	if(Queue->rear == NULL){
		Queue->front = Queue->rear = ku_mmu_pte_info;

		return;
	}

	Queue->rear->next = ku_mmu_pte_info;
	Queue->rear = ku_mmu_pte_info;
}

//deQueue의 역할을 하는 메소드
struct ku_mmu_pte_info* deQueue(){

	if(Queue->front == NULL){
		return NULL;
	}else{
		struct ku_mmu_pte_info* temp = Queue->front;
		Queue->count -=1;

		Queue->front = Queue->front->next;

		if(Queue->front == NULL){
			Queue->rear = NULL;
		}

		temp->next = NULL;
		return temp;
	}
}

void *ku_mmu_init(unsigned int mem_size, unsigned int swap_size){

	//피지컬 메모리, 스왑 스페이스의 0번 즉, 첫번째 공간은 이미 OS나 누군가에 의해 사용되고 있다.
	if((mem_size<=4)||(swap_size<=4)){
		printf("PFN 0 is uesd by OS \n");
		return NULL;
	}

	//사이즈 초기화
	ku_mmu_mem_size = mem_size;
	ku_mmu_swap_size = swap_size;


	//각 물리메모리, swap공간 할당
	ku_mmu_pmem = malloc(ku_mmu_mem_size);
	ku_mmu_swap_space = malloc(ku_mmu_swap_size);

	//free list 공간 할당, alloctae list 공간 할당
	pmem_free_list = malloc(ku_mmu_mem_size);
	swap_free_list = malloc(ku_mmu_swap_size);

	// 이미 존재하는 프로세스들을 관리하는 리스트 할당 및 초기화
	exist_p_list = malloc(sizeof(struct ku_mmu_pcb)*(ku_mmu_mem_size/4));
	struct ku_mmu_pcb tmp_pcb = { 0 , 0 };

	for(int i =0; i< (ku_mmu_mem_size/4); i++){
		exist_p_list[i] = tmp_pcb;
	}

	//Queue 공간 할당
	Queue = (struct FIFO_QUEUE*)malloc(sizeof(struct FIFO_QUEUE));

	Queue->count = 0;
	Queue->front = NULL;
	Queue->rear = NULL;

	//나머지 공간 초기화
	for(int i =0; i<ku_mmu_mem_size; i++){
		ku_mmu_pmem[i] = 0; //0으로 초기화 (사용중이지 않음)
		pmem_free_list[i] = 0; //0일때는 사용중이지 않은 상태
	}

	for(int i =0; i<ku_mmu_swap_size; i++){
		ku_mmu_swap_space[i] = 0; //0으로 초기화 (사용중이지 않음)
		swap_free_list[i] = 0; //0일때는 사용중이지 않은 상태
	}


	for(int i =0;i<4;i++){
		ku_mmu_swap_space[i] = 1;
		ku_mmu_pmem[i] = 1;
		pmem_free_list[i] = 1; //0번째 page frame은 이미 사용 중이다. (os의 의해)
		swap_free_list[i] = 1; //0번째 swap space는 사용하지 않는다.
	}

	return ku_mmu_pmem;
}


int ku_run_proc(char pid, struct ku_pte **ku_cr3){

	//이미 pcb가 생성된 (프로세스가 생성 된) pid 의 경우를 고려한다.
	for(int i =0 ; i<(ku_mmu_mem_size/4); i++){
		if(exist_p_list[i].pid == pid){
			*ku_cr3 = exist_p_list[i].cr3;

			// printf(" pid : %d , cr3 : %p , ku_cr3 : %p \n", pid, exist_p_list[i].cr3, *ku_cr3); //cr3 체크
			return 0;
		}
	}

	//새로운 pid 일시 PCB를 생성 해줌으로서 물리메모리 공간에 page를 할당 해준다.
	struct ku_mmu_pcb pcb_tmp = { pid, 0 };

	//새로운 pcb에 해당하는 page table 공간 할당 / 초기화 (page Table은 p_mem에 있지 않으므로 임의로 지정
	ku_mmu_pageTable = malloc(sizeof(struct ku_pte)*(256));
	struct ku_pte tmp_pte = { 0 }; //page table

	for(int i =0; i<(256);i++){
		ku_mmu_pageTable[i] = tmp_pte; //page table의 모든 pte값을 0으로 초기화
	}

	//새로 생성한 pid에 대한 pcb 생성 및 초기화
	pcb_tmp.cr3 = ku_mmu_pageTable;
	pcb_tmp.pid = pid;
	*ku_cr3 = pcb_tmp.cr3;

	for(int i =0; i<(ku_mmu_mem_size/4);i++){
		if(exist_p_list[i].pid == 0){
			exist_p_list[i] = pcb_tmp;

			break;
		}
	}

	//cr3 체크 printf(" pid : %d , cr3 : %p , ku_cr3 : %p \n", pcb_tmp.pid, pcb_tmp.cr3, *ku_cr3);

	return 0;

}

int ku_page_fault(char pid, char va){

	struct ku_pte* ptbr =(struct ku_pte*) malloc(sizeof(struct ku_pte));
	struct ku_pte* create_pte = (struct ku_pte*)malloc(sizeof(struct ku_pte));
	struct ku_pte tmp = { 0 };
	unsigned char vpn;



	//이미 존재하고 있는 프로세스의 경우 그 프로세스의 ptbr을 가져온다.
	for(int i=0 ; i<ku_mmu_mem_size/4; i++){

		if(exist_p_list[i].pid == pid){
			ptbr = exist_p_list[i].cr3;
			break;
		}
	}

	// printf("ptbr : %p , cr3 : %p ", ptbr, exist_p_list[i].cr3);


	for(int i=4;i<ku_mmu_mem_size;i+=4){

		if(pmem_free_list[i]==0){ //free 리스트에서 할당할 공간 찾았을 시

			for(int j =0;j<4;j++){
				pmem_free_list[i+j] = 1;
			}

			//vpn 연산, ptbr을 이용한 pte 위치 찾기 및 pte 비트 바꿔주기
			vpn = (va & PFN_MASK) >> 2;

			create_pte = ptbr + vpn; //pte의 위치가 나온다.
			create_pte->entry = (((i/4)<<2) | 0x01);

			// pid , ptbr, va, index_pmem, index_swap, next  
			struct ku_mmu_pte_info* temp;
			temp = createInfo(pid, ptbr, va, i, 0);

			enQueue(temp);


			return 0;

		}
	}



	//free list의 모든 공간이 찼을 때 (swap out 필요)
	//swap out해주는 과정
	for(int g=4; g<ku_mmu_swap_size; g+=4){ //swap size

		if(swap_free_list[g]==0){ //swap space의 할당되지 않은 부분을 찾는다

			struct ku_mmu_pte_info* temp;
			temp = deQueue();

			for(int k =0; k<4; k++){ //찾았다면 4byte 만큼의 page를 swapspace에 사용하고 있다고 할당
				swap_free_list[g+k] = 1;
			}

			//swap out 해준 p_mem 공간 빈공간으로 만들어주기
			for(int q = temp->index_in_pmem_free; q<(temp->index_in_pmem_free+4); q++){
				pmem_free_list[q] = 0;
			}

			struct ku_pte* swap_ptbr = (struct ku_pte*)malloc(sizeof(struct ku_pte));
			struct ku_pte* swap_create_pte = (struct ku_pte*)malloc(sizeof(struct ku_pte));

			swap_ptbr = temp->cr3;

			vpn = (temp->va & PFN_MASK) >> 2;
			swap_create_pte = swap_ptbr + vpn;

			//상위 7비트는 swap space, 하위 1비트는 present bit을 설정
			swap_create_pte->entry = (((g/4)<<1) & 0xFE); //present bit을 0으로 set해줌으로서 swap out됨을 나타낸다. 그러면서 상위 7bit에는 swap space offset을 넣는다.

			//swap space에 할당해준 index : g 를 저장, 물리 메모리 공간에는 할당이 안되어있으므로 0으로 만든다.
			temp->index_in_swap_free = g;
			temp->index_in_pmem_free = 0;

			for(int m =4; m<ku_mmu_mem_size; m+=4){

				if(pmem_free_list[m]==0){ //free 리스트에서 할당할 공간 찾았을 시

					for(int f =0;f<4;f++){
						//pmem 사용중이라고 표시
						pmem_free_list[m+f] = 1;
					}

					//vpn 연산, ptbr을 이용한 pte 위치 찾기 및 pte 비트 바꿔주기
					vpn = (va & PFN_MASK) >> 2;

					create_pte = ptbr + vpn;
					create_pte->entry = (((m/4)<<2) | 0x01);
					// create_pte->entry = ((ku_mmu_pmem[i]<<2) | 0x01);

					// pid , ptbr, va, index_pmem, index_swap, next
					struct ku_mmu_pte_info* temp;
					temp = createInfo(pid, ptbr, va, m, 0);

					enQueue(temp);

					return 0;

				}
			}
		}


	}
	//물리 메모리도 swap space도 둘다 꽉 찬 상태 return -1 해준다.
	return -1;
}

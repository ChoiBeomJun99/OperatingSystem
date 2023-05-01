#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>

int timeCount = 0; //SIGALRM이 발생하여 handler가 얼만큼 불렸는지를 count 하는 전역변수이다. 즉, 프로그램의 실행 된 시간을 의미한다.
int totalProcessCount = 0; // ku_mlfq 의 첫번째 인자로 받은 실행시킬 프로세스의 개수를 의미한다.
int ts; // ku_mlfq 의 두번째 인자로 받은 프로그램을 실행시킬 시간을 의미한다.

struct Pcb{ // Pcb를 구조체로 정의하였다. 실제로는 더 많은 정보가 있지만 구현을 위해 추상화하였다.
	int priority;
	int pid;
	int exeCount; //time allotment를 위한 해당 프로세스가 실행한 횟수를 세는 변수이다.
};

struct Node {
	struct Pcb *data; //노드안에 들어갈 데이터는 프로세스 즉, children 중 하나이다.
	struct Node* next;
};

struct Queue { //queue에는 자신의 priorityLevel, Queue안 몇개의 데이터가있는지(count) 그리고 rear, front가 있다.
	int level;
	int count;
	struct Node *front, *rear;
};

//각 priority 레벨에 맞는 queue들을 전역변수로 정의하였다.
struct Queue* level1;
struct Queue* level2;
struct Queue* level3;


//Pcb를 만드는 초기화 함수이다.
struct Pcb* createPcb(int pid, int priority, int exeCount){

	struct Pcb* temp = (struct Pcb*)malloc(sizeof(struct Pcb));
	temp->pid = pid;
	temp->priority = priority;
	temp->exeCount = exeCount;

	return temp;
}

//Node를 만드는 초기화 함수이다.
struct Node* createNode(struct Pcb* p)
{
	struct Node* temp = (struct Node*)malloc(sizeof(struct Node));
	temp->data =p;
	temp->next = NULL;

	return temp;
}

//Queue를 만드는 초기화 함수이다.
struct Queue* createQueue(int level)
{
	struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
	q->front = q->rear = NULL;
	q->level = level;
	q->count = 0;
	return q;
}

//enQueue를 구현한 메소드이다. 넣을 곳인 queue와 넣을 것인 node를 인자로 받는다.
void enQueue(struct Queue* q, struct Node* node)
{
	q->count +=1;
	if (q->rear == NULL) { //Queue에 아무것도 없던 상황
		q->front = q->rear = node;
		return;
	}

	q->rear->next = node;
	q->rear = node;

}

//deQueue를 구현한 메소드이다. 넣을 곳인 queue를 인자로 받고 deQueue를 통해 Queue에서 나온 Node를 반환값으로 받는다.
struct Node* deQueue(struct Queue* q){

	//Queue에 아무것도 없어서 Dequeue를 할 수 없는 상황
	if (q->front == NULL){
		return NULL;
	}else{
		struct Node* temp = q->front;
		q->count -=1;

		q->front = q->front->next;

		//queue가 비어버린 상황
		if (q->front == NULL){
			q->rear = NULL;
		}

		temp->next = NULL;
		return temp;
	}

}


void decreaseLevel(struct Node* node){ //time allotment 가 2s가 되었을 때 해당 프로세스의 priorityLevel을 감소해주기 위한 메소드이다.

	if(node->data->priority==3){ //만약 time allotment가 2s가 된 노드의 priority 레벨이 3이었으면 level2 Queue로 enQueue 해준다.
		enQueue(level2, node);

	}else if(node->data->priority==2){ //만약 time allotment가 2s가 된 노드의 priority 레벨이 2이었으면 level1 Queue로 enQueue 해준다.
		enQueue(level1, node);
	}

	node->data->priority--; // 그 후 해당 노드의 priority속성 값도 낮추어준다.

}


//우선 contextSwitch 방식은 이러하다 해당 큐에서 deQueue를 통해 나온 노드(프로세스)를 실행(SIGCONT 시그널을 보냄)시킨다.
//그 후, time allotment(exeCount)를 검사하여 priority level을 낮추어 줄지 말지를 선택해서 해당 queue에 넣어준다.

void contextSwitch(struct Queue* q){ //context switch를 구현한 함수이다.

	if(q->count==totalProcessCount){ //이는 모든 노드가 level3 또는 level2 큐에 존재할때 실행한다.

		kill(q->rear->data->pid, SIGSTOP); //해당 큐 rear 노드의 프로세스를 중단.
		struct Node* node = deQueue(q);
		kill(node->data->pid, SIGCONT); //deQueue를 통해 해당 큐의 front를 반환값으로 받아 저장한다. 그 후, SIGCONT 시그널을 보내 프로세스를 실행시킨다.
		node->data->exeCount++; //실행하였으므로 실행시간 +1s

		if(node->data->exeCount%2==0){ //해당 노드의 프로세스 실행 시간이 해당 큐에서 2s가 되었다면 priorityLevel을 낮추고, 낮춘 큐에다가 enQueue해준다.
			decreaseLevel(node);
		}else{
			enQueue(q, node); //해당 노드의 프로세스 실행 시간이 해당 큐에서의 2s가 되지 않았으므로 같은 큐에다가 그대로 넣는다.  --> 이러한 방식은 밑에 조건에서도 진행된다.

		}

	}else{
		if(q->level==3){ //level3 , level2 큐에 노드들이 들어가 있을 때
			kill(level2->rear->data->pid, SIGSTOP);
			struct Node* node = deQueue(q);
			kill(node->data->pid, SIGCONT);
			node->data->exeCount++;

			if(node->data->exeCount%2==0){
				decreaseLevel(node);
			}else{
				enQueue(q, node);

			}
		}else if(q->level==2){ //level2, level1 큐에 노드들이 들어가 있을 때
			kill(level1->rear->data->pid, SIGSTOP);
			struct Node* node = deQueue(q);
			kill(node->data->pid, SIGCONT);
			node->data->exeCount++;

			if(node->data->exeCount%2==0){
				decreaseLevel(node);
			}else{
				enQueue(q, node);
			}

		}else{ // 모든 노드들이 level1에 들어가 있을 때

			kill(q->rear->data->pid, SIGSTOP);
			struct Node* node = deQueue(q);
			kill(node->data->pid, SIGCONT);
			node->data->exeCount++;

			enQueue(q, node);
		}
	}

}


void boost(){ // boost를 구현한 함수이다.

	if(level3->count == totalProcessCount){ // 모든 노드가 level3에 들어있을때의 boost방법
		struct Node* tmp = level3->front;

		while(tmp != NULL){//모든 프로세스의 priorityLevel을 3으로 만들고 실행시간(time allotment) 또한 0으로 초기화 시켜준다.
			tmp->data->exeCount =0;
			tmp->data->priority = 3;
			tmp = tmp->next;
		}

		contextSwitch(level3); //boost 이후 contextSwitch를 실행한다. schedular(handler) 함수에서 boost를 해주느라 실행이 되지 못한 contextSwitch를 여기서 해준다.

	}else{

		if(level1->count == 0){  // 노드들이 level3과 level2에 존재할때 또는 모든 노드들이 level2에만 존재할때의 boost 방법이다

			while(level2->count > 0){ //level2의 모든 노드들을 순서를 지켜 level3으로 enQueue 해준다.
				struct Node* node = deQueue(level2);
				node->data->priority = 3;
				enQueue(level3, node);
			}

			struct Node* node = level3->front;

			while(node != NULL){ // level3에 존재하는 모든 노드들의 실행시간(time allotment)을 0으로 초기화 시켜준다.
				node->data->exeCount =0;
				node = node->next;
			}


			contextSwitch(level3); //boost 이후 contextSwitch를 실행한다. schedular(handler) 함수에서 boost를 해주느라 실행이 되지 못한 contextSwitch를 여기서 해준다.

		}else{ // 노드들이 level2과 level1에 존재할때 또는 모든 노드들이 level1에만 존재할때의 boost 방법이다

			while(level2->count > 0){ //level2의 모든 노드들을 순서를 지켜 level3으로 enQueue 해준다.
				struct Node* node = deQueue(level2);
				node->data->priority = 3;
				enQueue(level3, node);
			}

			while(level1->count > 0){ //level1의 모든 노드들을 순서를 지켜 level3으로 enQueue 해준다.
				struct Node* node = deQueue(level1);
				node->data->priority = 3;
				enQueue(level3, node);
			}

			struct Node* node = level3->front;

			while(node != NULL){ // level3에 존재하는 모든 노드들의 실행시간(time allotment)을 0으로 초기화 시켜준다.
				node->data->exeCount =0;
				node = node->next;
			}

			contextSwitch(level3); //boost 이후 contextSwitch를 실행한다. schedular(handler) 함수에서 boost를 해주느라 실행이 되지 못한 contextSwitch를 여기서 해준다.

		}
	}
}


void schedular(int sig){ //SIGALRM이 발생했을때 호출되는 핸들러 함수이다.

	timeCount++; //핸들러가 얼마나 불렸는지 즉, 핸들러는 1초마다 호출됨으로 프로그램이 얼마나 실행되었는지를 나타낸다.

	if(timeCount>ts){ //우리가 실행할 시간보다 더 초과된 경우 schedular 즉, 핸들러를 실행하지 않고 바로 return 해준다.
		return;
	}else{
		if(sig == SIGALRM){ //시그널(sig)이 SIGALRM 이면 실행
			if(timeCount%10==0){ //10초마다 boost를 진행한다.
				boost();
			}else{
				if(level3->count!=0){ //level3 queue에 노드가 존재할때 실행
					contextSwitch(level3);
				}else if(level2->count!=0){ //level3 에는 노드가 없고 level2 queue에 노드가 존재할때 실행
					contextSwitch(level2);
				}else{
					contextSwitch(level1); //level3, level2에는 노드가 없고 level1 queue에 노드가 존재할때 실행
				}
			}
		}
	}

}

void start(){ //타이머 세팅 후 첫번째 SIGALRM 울리기 전 실행 하는 함수이다. SIGALRM을 통해 context switch가 됨으로 그 전 과정을 나타낸다.

	struct Node* node = deQueue(level3);
	kill(node->data->pid, SIGCONT);
	node->data->exeCount++;
	enQueue(level3, node);
	timeCount++;

}


int main(int argc, char *argv[]){

	//각 레벨별 priority Queue 초기화 진행
	level3 = createQueue(3);
	level2 = createQueue(2);
	level1 = createQueue(1);

	//실행 할시간을 ts에 지정 (timeslice의 개수)
	ts = atoi(argv[2]);

	//argv[1] 만큼 자식 process 생성한다. 즉, fork()를 argv[1] 만큼 호출해준 뒤 execl 을 통해 ku_app을 입혀준다.
	pid_t pids[atoi(argv[1])], pid;
	int processCount =0;
	totalProcessCount = atoi(argv[1]);

	while(processCount < atoi(argv[1])){ //프로세스를 생성하고 execl하는 과정 부모 프로세스는 자식 프로세스들을 level3 Queue에 enQueue를 해준다. while문을 사용했다.

		pids[processCount] = fork();
		if(pids[processCount] < 0){ //fork() 오류
			return -1;

		}else if(pids[processCount]==0){ //자식 프로세스

			int alpc = 65+processCount; //ku_app을 실행할때 인자가 필요한데, execl을 위한 세팅 과정이다.

			char* tmp = (char*)malloc(2*sizeof(char)); //문자열로 만들어 execl 인자로 넘겨준다.
			char alp = (char)alpc;
			tmp = &alp;
			tmp[1] = NULL;

			execl( "./ku_app", "ku_app", tmp, NULL);


		}else{ //부모 프로세스

			enQueue(level3, createNode(createPcb(getpid()+processCount+1, 3, 0))); //부모 프로세스에서는 생성된 자식 프로세스를 생성 순서대로 level3 Queue에 enQueue 해준다.
		}

		processCount++;

	}
	sleep(5); //프로세스를 생성 후 Timer 세팅 전 5초간 준비할 시간을 마련해준다.

	//타이머 세팅 과정
	int which = ITIMER_REAL;
	struct itimerval value;
	struct sigaction sact;
	float count;

	//핸들러 세팅 과정
	sact.sa_handler = schedular;
	sact.sa_flags = SA_NOMASK;
	sigaction(SIGALRM,&sact,NULL);

	//타이머는 1초마다 SIGALRM을 보내도록 설정
	value.it_value.tv_sec = 1;
	value.it_value.tv_usec = 0;

	value.it_interval.tv_sec = 1;
	value.it_interval.tv_usec = 0;

	int reult = setitimer(which,&value,NULL); //타이머 setting

	//첫번째 자식 프로세스부터 시작
	start();

	while(timeCount<=ts); //while문을 돌면서 ku_mlfq 에서 두번째 인자값으로 받은 ts만큼 실행시킨다. 그 이상이 되면 프로그램은 종료한다.

	return 0;
}

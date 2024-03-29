#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "threadPool.h"
#include "balanceBinarySearchTree.h"
#include <strings.h>
#include <sqlite3.h>
#include <string.h>

#define SERVER_PORT     8080
#define LISTEN_MAX      128
#define BUFFER_SIZE 300

#define MIN_THREADS     10
#define MAX_THREADS     20
#define MAX_QUEUE_CAPACITY  10

#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16
#define BUFFER_SQL          150

/* 主界面选择 */
enum CLIENT_CHOICE
{
    REGISTER = 1,
    LOG_IN,
    EXIT,
};

/* 功能界面选择 */
enum FUNC_CHOICE
{
    PRIVATE_CHAT = 1,
    FRIEND_ADD,
    GROUP_CHAT,
    GROUP_CREATE,
    INTERNAL_EXIT,
};

typedef struct chatRoom
{
    BalanceBinarySearchTree * online;
    int communicateFd;
    pthread_mutex_t mutex;
} chatRoom;


typedef struct clientNode
{
    char loginName[DEFAULT_LOGIN_NAME];
    int communicateFd;
} clientNode;


/* 锁 */
pthread_mutex_t g_mutex;

/* AVL比较器：以登录名做比较 */
int compareFunc(void * val1, void * val2)
{
    clientNode *node1 = (clientNode *)val1;
    clientNode *node2 = (clientNode *)val2;

    int ret = strncmp(node1->loginName, node2->loginName, sizeof(node1->loginName));
    if(ret > 0)
    {
        return 1;
    }
    else if(ret < 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

/* AVL打印器 */
int printfFunc(void * val)
{
    clientNode *client = (clientNode *)val;
    printf("%s\n", client->loginName);
}


/* 信息写入判断函数 */
static void writeMessage(int fd, char *message, int messageSize);
/* 信息读取判断函数 */
static void readMessage(int fd, char *message, int messageSize);
/* 数据读取判断函数 */
static void readData(int fd, int *data, int dataSize);
/* 聊天内容插入数据库函数 */
static void ChatContentInsert(sqlite3 *db, char *filename, char *username, char *word);
/* 注册信息插入数据库函数 */
static void RegesiterContentInsert(sqlite3 *db, char *id, char *password);
/* 用户退出函数 */
static int userExit(int socketfd, BalanceBinarySearchTree * onlineList, clientNode *client);
/* 用户登录函数 */
static int userLogin(int socketfd, BalanceBinarySearchTree * onlineList, clientNode *client, char *loginName, char *loginPawd);
/* 用户注册函数 */
static void userRegister(int socketfd, char *loginName, char *loginPawd);
/* 用户私聊函数 */
static void userPrivateChat(int socketfd, BalanceBinarySearchTree *onlineList, int *mode);
/* 用户群聊函数 */
static void userGroupChat(int socketfd, BalanceBinarySearchTree *onlineList, int *mode);
/* 建群 */
static void createGroup(int socketfd, BalanceBinarySearchTree *onlineList, int *mode);

/* 回调函数，用于处理查询结果 */
int callback(void* data, int argc, char** argv, char** azColName) 
{
    if (argc == 0) 
    {
        *(int *)data = 0;
    } 
    else 
    {
        *(int *)data = 1;
    }
    return 0;
}

void * chatHander(void * arg)
{
    /* 接收传递过来的结构体 */
    chatRoom * chat = (chatRoom *)arg;
    /* 通信句柄 */
    int acceptfd = chat->communicateFd;
    /* 在线列表 */
    BalanceBinarySearchTree * onlineList = chat->online;

    /* 新建用户结点 */
    char loginName[DEFAULT_LOGIN_NAME];
    bzero(loginName, sizeof(loginName));
    char loginPawd[DEFAULT_LOGIN_PAWD];
    bzero(loginPawd, sizeof(loginPawd));
    /* 创建客户端结点 */
    clientNode client;
    bzero(&client, sizeof(client));
    bzero(client.loginName, sizeof(client.loginName));

    int flag = 0;
    int choice = 0;
    /* 程序运行 */
    while (1)
    {
        /* 定义指向数据库的指针 */
        sqlite3 * chatRoomDB = NULL;
        char * ermsg = NULL;
        char sql[BUFFER_SQL];
        bzero(sql, sizeof(sql));
        int found= 0;
        int ret = 0;
        /* 循环接收选择的功能 */
        
        int readBytes = read(acceptfd, &choice, sizeof(choice));
        if(readBytes < 0)
        {
            printf("read error\n");
            close(acceptfd);
            pthread_exit(NULL);
            break;
        }
        else if(readBytes == 0)
        {
            printf("客户端下线\n");
            close(acceptfd);
            pthread_exit(NULL);
            break;
        }
        /* 当前客户端聊天文件名 */
        char clientHostBuffer[BUFFER_SIZE];
        bzero(clientHostBuffer, sizeof(clientHostBuffer));
        /* 对方客户端聊天文件名 */
        char clientGuestBuffer[BUFFER_SIZE];
        bzero(clientGuestBuffer, sizeof(clientGuestBuffer));

        switch (choice)
        {
        /* 注册功能 */
        case REGISTER:
            /* 读取用户输入的登录名和密码 */
            bzero(loginName, sizeof(loginName));
            bzero(loginPawd, sizeof(loginPawd));
            readMessage(acceptfd, loginName, sizeof(loginName) - 1);
            readMessage(acceptfd, loginPawd, sizeof(loginPawd) - 1);

            /* 用户注册函数 */
            userRegister(acceptfd, loginName, loginPawd);
            break;

        /* 登录功能 */
        case LOG_IN:
            /* 读取用户输入的登录名和密码 */
            bzero(loginName, sizeof(loginName));
            bzero(loginPawd, sizeof(loginPawd));
            readMessage(acceptfd, loginName, sizeof(loginName) - 1);
            readMessage(acceptfd, loginPawd, sizeof(loginPawd) - 1);

            /* 用户登录函数 */
            flag = userLogin(acceptfd, onlineList, &client, loginName, loginPawd);
            break;
        
        /* 退出功能 */
        case EXIT:
            writeMessage(acceptfd, "客户端退出", sizeof("客户端退出"));
            printf("客户端退出\n");
            pthread_exit(NULL);
            break;

        default:
            break;
        }

        /* 接收缓冲区 */
        char recvBuffer[BUFFER_SIZE];
        bzero(recvBuffer, sizeof(recvBuffer));
        
        int mode = 0;
        while(flag)
        {
            /* 循环接收选择的功能 */
            int readBytes = read(acceptfd, &mode, sizeof(mode));
            if(readBytes < 0)
            {
                printf("read error\n");
                close(acceptfd);
                break;
            }
            else if(readBytes == 0)
            {
                printf("用户下线\n");
                close(acceptfd);
                break;
            }

            switch(mode)
            {
            /* 私聊功能 */
            case PRIVATE_CHAT:
                /* 用户私聊函数 */
                userPrivateChat(acceptfd, onlineList, &mode);
                break;

            /* 加好友功能 */
            case FRIEND_ADD:
                
                break;

            /* 群聊功能 */
            case GROUP_CHAT:
                /* 用户群聊函数 */
                userGroupChat(acceptfd, onlineList, &mode);
                break;

            /* 建群功能 */
            case GROUP_CREATE:
                /* 建群 */
                createGroup(acceptfd, onlineList, &mode);
                break;
            
            /* 退出功能 */
            case INTERNAL_EXIT:
                /* 用户退出函数 */
                flag = userExit(acceptfd, onlineList, &client);
                break;

            default:
                break;
            }
        }
    }
    pthread_exit(NULL);
}

int main()
{
    /* 初始化线程池 */
    // threadPool pool;

    // threadPoolInit(&pool, MIN_THREADS, MAX_THREADS, MAX_QUEUE_CAPACITY);

    /* 初始化锁：用来实现对在线列表的互斥访问 */
    pthread_mutex_init(&g_mutex, NULL);

    /* 创建数据库和表 */
    sqlite3 * chatRoomDB = NULL;
    /* 打开数据库 */
    int ret = sqlite3_open("chatRoom.db", &chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }

    /* 创建储存所有用户的表 */
    char * ermsg = NULL;
    const char * sql = "create table if not exists user (id text primary key not null, password text not null)";
    ret = sqlite3_exec(chatRoomDB, sql, NULL, NULL, &ermsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", ermsg);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    /* 打开数据库 */
    ret = sqlite3_open("groupList.db", &chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }

    /* 创建储存群信息的表 */
    const char * sql1 = "create table if not exists groupList (groupname text not null, username text not null)";
    ret = sqlite3_exec(chatRoomDB, sql1, NULL, NULL, &ermsg);
    if (ret != SQLITE_OK)
    {
        printf("create table error:%s\n", ermsg);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    struct sockaddr_in serverAddress;
    struct sockaddr_in clientAddress;

    /* 存储服务器信息 */
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    socklen_t serverAddressLen = sizeof(serverAddress);
    socklen_t clientAddressLen = sizeof(clientAddress);

    /* 设置端口复用 */
    int enableOpt = 1;
    ret = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (void *) &enableOpt, sizeof(enableOpt));
    if (ret == -1)
    {
        perror("setsockopt error");
        close(socketfd);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    /* 绑定服务器端口信息 */
    ret = bind(socketfd, (struct sockaddr *)&serverAddress, serverAddressLen);
    if (ret == -1)
    {
        perror("bind error");
        close(socketfd);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    ret = listen(socketfd, LISTEN_MAX);
    if (ret == -1)
    {
        perror("listen error");
        close(socketfd);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    /* 建立在线链表 */
    BalanceBinarySearchTree * onlineList;

    ret = balanceBinarySearchTreeInit(&onlineList, compareFunc, printfFunc);
    if (ret != 0)
    {
        perror("create online list error");
        close(socketfd);
        sqlite3_close(chatRoomDB);
        pthread_mutex_destroy(&g_mutex);
        // threadPoolDestroy(&pool);
        exit(-1);
    }

    chatRoom chat;
    bzero(&chat, sizeof(chat));

    chat.online = onlineList;
    
    int acceptfd = 0;
    /* 建立连接 */
    while (1)
    {
        acceptfd = accept(socketfd, (struct sockaddr *)&clientAddress, &clientAddressLen);
        if (acceptfd == -1)
        {
            perror("accept error");
            break;
        }

        chat.communicateFd = acceptfd;

        /* 创建线程 */
        pthread_t tid;
        pthread_create(&tid, NULL, chatHander, (void *)&chat);
        // taskQueueInsert(&pool, chatHander, (void *)&chat);
    }

    /* 销毁资源 */
    close(acceptfd);
    close(socketfd);
    
    pthread_mutex_destroy(&g_mutex);
    balanceBinarySearchTreeDestroy(onlineList);
    sqlite3_close(chatRoomDB);
    // threadPoolDestroy(&pool);

    return 0;
}

/* 用户退出函数 */
static int userExit(int socketfd, BalanceBinarySearchTree *onlineList, clientNode *client)
{
    int flag = 1;
    /* 加锁，保证在线列表的互斥访问 */
    pthread_mutex_lock(&g_mutex);
    /* 从在线列表中删除用户结点 */
    balanceBinarySearchTreeDelete(onlineList, (void *)client);
#if 0
    if(onlineList->root == NULL)
    {
        printf("null\n");
    }
    else
    {
        printf("not null\n");
    }
#endif
    if(balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)client))
    {
        /* 未删除结点 */
        writeMessage(socketfd, "用户退出失败", sizeof("用户退出失败"));
        printf("用户退出失败");
    }
    else
    {
        /* 已删除结点 */
        writeMessage(socketfd, "用户退出", sizeof("用户退出"));
        printf("用户退出");
        flag = 0;
    }
    /* 解锁 */
    pthread_mutex_unlock(&g_mutex);
    return flag;
}

/* 用户私聊函数 */
static void userPrivateChat(int socketfd, BalanceBinarySearchTree *onlineList, int *mode)
{
    char objectName[DEFAULT_LOGIN_NAME];
    bzero(objectName, sizeof(objectName));
    char sendBuffer[BUFFER_SIZE];
    bzero(sendBuffer, sizeof(sendBuffer));
    while(1)
    {
        int flag = 0;
        /* 读取用户输入的私聊对象 */
        bzero(objectName, sizeof(objectName));
        readMessage(socketfd, objectName, sizeof(objectName) - 1);
        if(strncmp(objectName, "q", sizeof("q")) == 0)
        {
            *mode = 0;
            break;
        }
        else if(strncmp(objectName, "该对象非好友", sizeof("该对象非好友")) == 0)
        {
            break;
        }
        /* 创建对象客户端结点 */
        clientNode objclient;
        bzero(&objclient, sizeof(objclient));
        bzero(objclient.loginName, sizeof(objclient.loginName));
        
        AVLTreeNode *object = NULL;
        int objectfd = 0;

        /* 查看对方是否在线 */
        strncpy(objclient.loginName, objectName, strlen(objectName));
        if(balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&objclient))
        {
            object =  baseAppointValGetAVLTreeNode(onlineList, (void *)&objclient);
            objclient = *(clientNode *)(object->data);
            objectfd = objclient.communicateFd;
            flag = 1;
            writeMessage(socketfd, "对方在线", sizeof("对方在线"));
        }
        else
        {
            bzero(objclient.loginName, sizeof(objclient.loginName));
            flag = 0;
            writeMessage(socketfd, "对方不在线", sizeof("对方不在线"));
        }
        int ret = 0;
        while(flag)
        {
            bzero(sendBuffer, sizeof(sendBuffer));
            ret = read(socketfd, sendBuffer, sizeof(sendBuffer));

            if(strncmp(sendBuffer, "q", sizeof("q")) == 0)
            {
                writeMessage(objectfd, "退出聊天", sizeof("退出聊天"));
                flag = 0;
                break;
            }
            if(ret > 0)
            {
                writeMessage(objectfd, sendBuffer, strlen(sendBuffer));
            }
        }
    }
}

/* 用户群聊函数 */
static void userGroupChat(int socketfd, BalanceBinarySearchTree *onlineList, int *mode)
{
    char groupName[DEFAULT_LOGIN_NAME];
    bzero(groupName, sizeof(groupName));
    char objectName[DEFAULT_LOGIN_NAME];
    bzero(objectName, sizeof(objectName));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    char sendBuffer[BUFFER_SIZE];
    bzero(sendBuffer, sizeof(sendBuffer));

    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    char **result = NULL;
    int row = 0;
    int column = 0;
    while(1)
    {
        int flag = 0;
        /* 读取用户输入的群名 */
        bzero(groupName, sizeof(groupName));
        readMessage(socketfd, groupName, sizeof(groupName) - 1);
        if(strncmp(groupName, "q", sizeof("q")) == 0)
        {
            *mode = 0;
            break;
        }
        else if(strncmp(groupName, "此群未创建", sizeof("此群未创建")) == 0)
        {
            writeMessage(socketfd, "此群未创建", strlen("此群未创建"));
        }
        else
        {
            /* 打开数据库 */
            int ret = sqlite3_open("groupList.db", &chatRoomDB);
            if(ret != SQLITE_OK)
            {
                perror("sqlite open error");
                exit(-1);
            }
            sprintf(sql, "select * from groupList where groupname = '%s'", groupName);
            sqlite3_get_table(chatRoomDB, sql, &result, &row, &column, &ermsg);
            if(ret != SQLITE_OK)
            {
                perror("sqlite get table error");
                exit(-1);
            }
        }
        /* 创建对象客户端结点 */
        clientNode objclient;
        bzero(&objclient, sizeof(objclient));
        bzero(objclient.loginName, sizeof(objclient.loginName));
        
        AVLTreeNode *object = NULL;
        int objectfd = 0;
        int exit = 0;

        if(row >= 1)
        {
            writeMessage(socketfd, "进入群聊", sizeof("进入群聊"));
            flag= 1;
            while(flag)
            {
                int ret = 0;
                bzero(sendBuffer, sizeof(sendBuffer));
                ret = read(socketfd, sendBuffer, sizeof(sendBuffer));
                if(strncmp(sendBuffer, "q", sizeof("q")) == 0)
                {
                    writeMessage(socketfd, "退出群聊", sizeof("退出群聊"));
                    flag = 0;
                }
                for(int idx = 1; idx <= row; idx++)
                {
                    bzero(objectName, sizeof(objectName));
                    strncpy(objectName, result[idx * column + 1], sizeof(result[idx * column + 1]));
                    /* 查看对方是否在线 */
                    bzero(&objclient, sizeof(objclient));
                    bzero(objclient.loginName, sizeof(objclient.loginName));
                    strncpy(objclient.loginName, objectName, strlen(objectName));
                    if(balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)&objclient))
                    {
                        object =  baseAppointValGetAVLTreeNode(onlineList, (void *)&objclient);
                        objclient = *(clientNode *)(object->data);
                        objectfd = objclient.communicateFd;
                        if(objectfd != socketfd)
                        {
                            if(ret > 0 && strncmp(sendBuffer, "q", sizeof("q")) != 0)
                            {
                                writeMessage(objectfd, sendBuffer, strlen(sendBuffer));
                            }
                        }
                    }
                }
            }
        }
        else if(row < 1)
        {
            writeMessage(socketfd, "进入群聊失败", sizeof("进入群聊失败"));
        }
    }
}

/* 用户登录函数 */
static int userLogin(int socketfd, BalanceBinarySearchTree * onlineList, clientNode *client, char *loginName, char *loginPawd)
{
    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    int found = 0;
    int flag = 0;
    /* 写入客户端结点 */
    strncpy(client->loginName, loginName, sizeof(loginName));
    client->communicateFd = socketfd;

    /* 打开数据库连接 */
    int ret = sqlite3_open("chatRoom.db", &chatRoomDB);
    if (ret != SQLITE_OK)
    {
        perror("sqlite open error");
        close(socketfd);
        pthread_exit(NULL);
    }

    /* 查询数据库，验证用户名和密码 */
    sprintf(sql, "select * from user where id ='%s'", loginName);
    ret = sqlite3_exec(chatRoomDB, sql, callback, &found, &ermsg);
    if (ret != SQLITE_OK)
    {
        printf("Login failed: %s\n", ermsg);
        close(socketfd);
        pthread_exit(NULL);
    }
    /* 用户存在 */
    if(found == 1)
    {
        found = 0;
        sprintf(sql, "select * from user where id ='%s' and password='%s'", loginName, loginPawd);
        ret = sqlite3_exec(chatRoomDB, sql, callback, &found, &ermsg);
        if (ret != SQLITE_OK)
        {
            printf("Login failed: %s\n", ermsg);
            close(socketfd);
            pthread_exit(NULL);
        }
        /* 密码正确 */
        if(found == 1)
        {
            /* 加锁，保证在线列表的互斥访问 */
            pthread_mutex_lock(&g_mutex);
            /* 在线列表中查询账号 */
            int existance = balanceBinarySearchTreeIsContainAppointVal(onlineList, (void *)client);
            if(existance == 0)
            {
                /* 将用户添加到在线列表中 */
                balanceBinarySearchTreeInsert(onlineList, (void *)client);
                /* 显示在线用户 */
                printf("在线用户：\n");
                balanceBinarySearchTreeLevelOrderTravel(onlineList);
                /* 登录成功 */
                writeMessage(socketfd, "登录成功", sizeof("登录成功"));
                /* 登录标志位置1 */
                flag = 1;
            }
            else if(existance == 1)
            {
                /* 用户已在别处登录 */
                writeMessage(socketfd, "用户已在别处登录", sizeof("用户已在别处登录"));
            }
            /* 解锁 */
            pthread_mutex_unlock(&g_mutex);
            
        }
        /* 密码错误 */
        else
        {
            writeMessage(socketfd, "密码错误", sizeof("密码错误"));
        }
    }
    /* 用户不存在 */
    else
    {
        writeMessage(socketfd, "用户不存在", sizeof("用户不存在"));
    }
    /* 关闭数据库 */
    sqlite3_close(chatRoomDB);

    return flag;
}

/* 用户注册函数 */
static void userRegister(int socketfd, char *loginName, char *loginPawd)
{
    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    int found = 0;
    /* 加锁以保证在线列表的互斥访问 */
    pthread_mutex_lock(&g_mutex);

    /* 打开数据库 */
    int ret = sqlite3_open("chatRoom.db", &chatRoomDB);
    if(ret != SQLITE_OK)
    {
        perror("sqlite open error");
        pthread_exit(NULL);
    }

    /* 检查用户是否已经存在 */
    sprintf(sql, "select * from user where id = '%s'", loginName);
    ret = sqlite3_exec(chatRoomDB, sql, callback, &found, &ermsg);
    if(ret != SQLITE_OK)
    {
        perror("sqlite open error");
        pthread_exit(NULL);
    }
    if(found == 1)
    {
        /* 用户已存在 */
        writeMessage(socketfd, "注册失败，用户已存在", sizeof("注册失败，用户已存在"));
    }
    else
    {
        /* 将新用户添加到数据库 */
        RegesiterContentInsert(chatRoomDB, loginName, loginPawd);
        /* 注册成功 */
        writeMessage(socketfd, "注册成功", sizeof("注册成功"));
    }
    
    /* 关闭数据库 */
    sqlite3_close(chatRoomDB);
    
    /* 解锁 */
    pthread_mutex_unlock(&g_mutex);
}

/* 信息写入判断函数 */
static void writeMessage(int fd, char *message, int messageSize)
{
    int ret = write(fd, message, messageSize);
    if (ret < 0)
    {
        perror("write error");
        close(fd);
        exit(-1);
    }
}

/* 信息读取判断函数 */
static void readMessage(int fd, char *message, int messageSize)
{
    int ret = read(fd, message, messageSize);
    if (ret < 0)
    {
        perror("read error");
        close(fd);
        exit(-1);
    }
}

/* 数据写入判断函数 */
static void readData(int fd, int *data, int dataSize)
{
    int ret = read(fd, data, dataSize);
    if (ret < 0)
    {
        perror("read error");
        close(fd);
        exit(-1);
    }
}

/* 聊天内容插入数据库函数 */
static void ChatContentInsert(sqlite3 *db, char *filename, char *username, char *word)
{
    const char *sql = NULL;
    /* 打开数据库：如果数据库不存在，那么就创建 */
    int ret = sqlite3_open(filename, &db);
    if(ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }
    /* 预编译的SQL语句对象 */
    sqlite3_stmt *stmt;
    sql = "insert into chat values (?, ?)";
    
    /* 准备SQL语句 */
    ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_prepare_v2 error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }

    /* 绑定文本参数 */
    /* 用户名 */
    ret = sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_bind_text error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }
    /* 输入内容 */
    ret = sqlite3_bind_text(stmt, 2, word, -1, SQLITE_STATIC);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_bind_text error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }

    /* 执行SQL语句 */
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) 
    {
        printf("sqlite3_step error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }
    else 
    {
        printf("success\n");
    }

    /* 清理资源 */
    sqlite3_finalize(stmt);
}

/* 注册信息插入数据库函数 */
static void RegesiterContentInsert(sqlite3 *db, char *id, char *password)
{
    const char *sql = NULL;
    /* 预编译的SQL语句对象 */
    sqlite3_stmt *stmt;
    sql = "insert into user values (?, ?)";
    
    /* 准备SQL语句 */
    int ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_prepare_v2 error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }

    /* 绑定文本参数 */
    /* 用户名 */
    ret = sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_bind_text error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }
    /* 密码 */
    ret = sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);
    if (ret != SQLITE_OK) 
    {
        printf("sqlite3_bind_text error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }

    /* 执行SQL语句 */
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) 
    {
        printf("sqlite3_step error\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        exit(-1);
    }
    else 
    {
        printf("success\n");
    }

    /* 清理资源 */
    sqlite3_finalize(stmt);
}

/* 建群 */
static void createGroup(int socketfd, BalanceBinarySearchTree *onlineList, int *mode)
{
    char groupName[DEFAULT_LOGIN_NAME];
    bzero(groupName, sizeof(groupName));
    char objectName[DEFAULT_LOGIN_NAME];
    bzero(objectName, sizeof(objectName));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    char sendBuffer[BUFFER_SIZE];
    bzero(sendBuffer, sizeof(sendBuffer));
    char choice[DEFAULT_LOGIN_NAME];
    bzero(choice, sizeof(choice));

    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    char **result = NULL;
    int row = 0;
    int column = 0;
    int ret = 0;
    while(1)
    {
        int flag = 0;
        /* 读取用户输入的群名 */
        bzero(groupName, sizeof(groupName));
        readMessage(socketfd, groupName, sizeof(groupName) - 1);
        if(strncmp(groupName, "q", sizeof("q")) == 0)
        {
            *mode = 0;
            break;
        }
        else if(strncmp(groupName, "此群未创建", sizeof("此群未创建")) == 0)
        {
            writeMessage(socketfd, "输入用户名", sizeof("输入用户名"));
            bzero(objectName, sizeof(objectName));
            readMessage(socketfd, objectName, sizeof(objectName) - 1);
            /* 打开数据库 */
            ret = sqlite3_open("groupList.db", &chatRoomDB);
            if(ret != SQLITE_OK)
            {
                perror("sqlite open error");
                exit(-1);
            }
            sprintf(sql, "select * from groupList where groupname = '%s' and username = '%s'", groupName, objectName);
            sqlite3_get_table(chatRoomDB, sql, &result, &row, &column, &ermsg);
            if(ret != SQLITE_OK)
            {
                perror("sqlite get table error");
                exit(-1);
            }
            writeMessage(socketfd, "创建完成", sizeof("创建完成"));
        }
        bzero(choice, sizeof(choice));
        readMessage(socketfd, choice, sizeof(choice) - 1);
        if(strncmp(choice, "y", sizeof("y")))
        {
            writeMessage(socketfd, choice, strlen(choice));
            flag = 1;
        }
        else if(strncmp(choice, "n", sizeof("n")))
        {
            writeMessage(socketfd, choice, strlen(choice));
            flag = 0;
            break;
        }
        if(flag)
        {
            /* 创建对象客户端结点 */
            clientNode objclient;
            bzero(&objclient, sizeof(objclient));
            bzero(objclient.loginName, sizeof(objclient.loginName));
            
            AVLTreeNode *object = NULL;
            int objectfd = 0;

            bzero(objectName, sizeof(objectName));
            readMessage(socketfd, objectName, sizeof(objectName));
            /* 打开数据库 */
            ret = sqlite3_open("groupList.db", &chatRoomDB);
            if(ret != SQLITE_OK)
            {
                perror("sqlite open error");
                exit(-1);
            }
            sprintf(sql, "select * from groupList where groupname = '%s' and username = '%s'", groupName, objectName);
            sqlite3_get_table(chatRoomDB, sql, &result, &row, &column, &ermsg);
            if(ret != SQLITE_OK)
            {
                perror("sqlite get table error");
                exit(-1);
            }
            writeMessage(socketfd, "添加完成", strlen("添加完成"));
        }
    }
}







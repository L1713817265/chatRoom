#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <sqlite3.h>
#include <pthread.h>

#define SERVER_PORT 8080
// #define SERVER_ADDR "172.30.149.120"
#define SERVER_ADDR "172.27.105.168"

#define DEFAULT_LOGIN_NAME  20
#define DEFAULT_LOGIN_PAWD  16
#define BUFFER_SIZE 300
#define BUFFER_SQL          100

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

typedef struct sendPackage
{
    char loginName[DEFAULT_LOGIN_NAME];
    int communicateFd;
} sendPackage;

/* 信息写入判断函数 */
static void writeMessage(int fd, char *message, int messageSize);
/* 信息读取判断函数 */
static void readMessage(int fd, char *message, int messageSize);
/* 数据写入判断函数 */
static void writeData(int fd, int *data, int dataSize);
/* 用户注册函数 */
static void userRegister(int socketfd);
/* 用户登录函数 */
static int userLogin(int socketfd, char *loginName);
/* 客户端退出函数 */
static void clientExit(int socketfd, int mainMenufd, int funcMenufd);
/* 用户退出函数 */
static int userExit(int socketfd);
/* 用户私聊函数 */
static int privateChat(int socketfd, char *loginName);
/* 用户群聊函数 */
static int groupChat(int socketfd, char *loginName);

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

/* 私聊接收信息线程 */
void * recvMessage(void *arg)
{
    /* 线程分离 */
    pthread_detach(pthread_self());
    char recvBuffer[BUFFER_SIZE];
    bzero(recvBuffer, sizeof(recvBuffer));
    
    int ret = 0;
    while(1)
    {
        bzero(recvBuffer, sizeof(recvBuffer));
        ret = read(*(int *)arg, recvBuffer, sizeof(recvBuffer));
        if(ret > 0)
        {
            if(strncmp(recvBuffer, "退出聊天", sizeof("退出聊天")) == 0)
            {
                pthread_exit(NULL);
            }
            else if(strncmp(recvBuffer, "退出群聊", sizeof("退出群聊")) == 0)
            {
                pthread_exit(NULL);
            }
            else
            {
                printf("%s\n", recvBuffer);
            }
        }
    }
}

int main()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1)
    {
        perror("socket error");
        exit(-1);
    }

    struct sockaddr_in localAddress;
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = htons(SERVER_PORT);

    int ret = inet_pton(AF_INET, SERVER_ADDR, &localAddress.sin_addr.s_addr);
    if (ret == -1)
    {
        perror("inet_pton error");
        close(socketfd);
        exit(-1);
    }

    socklen_t localAddressLen = sizeof(localAddress);

    ret = connect(socketfd, (struct sockaddr *)&localAddress, localAddressLen);
    if (ret == -1)
    {
        perror("connect error");
        close(socketfd);
        exit(-1);
    }

    /* 打开主菜单文件 */
    int mainMenu = open("mainMenu", O_RDONLY);
    if (mainMenu == -1)
    {
        perror("open mainMenu error");
        close(socketfd);
        exit(-1);
    }
    char mainMenuBuffer[BUFFER_SIZE];
    bzero(mainMenuBuffer, sizeof(mainMenuBuffer));
    readMessage(mainMenu, mainMenuBuffer, sizeof(mainMenuBuffer) - 1);

    /* 打开功能页面文件 */
    int funcMenu = open("funcMenu", O_RDONLY);
    if (funcMenu == -1)
    {
        perror("open funcMenu error");
        close(mainMenu);
        close(socketfd);
        exit(-1);
    }
    char funcMenuBuffer[BUFFER_SIZE];
    bzero(funcMenuBuffer, sizeof(funcMenuBuffer));
    readMessage(funcMenu, funcMenuBuffer, sizeof(funcMenuBuffer) - 1);

    /* 登录标志位 */
    int flag = 0;
    /* 开始执行功能 */
    int choice = 0;
    while(1)
    {
        choice = 0;
        char input[BUFFER_SIZE];
        bzero(input, sizeof(input));
        printf("%s\n", mainMenuBuffer);
        printf("请选择你需要的功能：\n");
        scanf("%d", &choice);
        fgets(input, sizeof(input), stdin);
        sscanf(input, "%d", &choice);
        if(choice >= 1 && choice <= 3)
        {
            writeData(socketfd, &choice, sizeof(choice));
        }
        /* 清空输入缓冲区 */
        system("clear");

        /* 登录用户名 */
        char loginName[BUFFER_SIZE];
        bzero(loginName, sizeof(loginName));
        switch (choice)
        {
        /* 注册 */
        case REGISTER:
            /* 用户注册并创建好友列表 */
            userRegister(socketfd);
            sleep(1);
            system("clear");
            break;
        
        /* 登录 */
        case LOG_IN:
            /* 用户登录函数 */
            flag = userLogin(socketfd, loginName);
            sleep(1);
            system("clear");
            break;
        
        /* 退出 */
        case EXIT:
            /* 客户端退出函数 */
            clientExit(socketfd, mainMenu, funcMenu);
            break;

        default:
            break;
        }

        int connect = 0;
        int readBytes = 0;
        /* 模式选择变量 */
        int mode = 0;
        while(flag)
        {
            mode = 0;
            char input2[BUFFER_SIZE];
            bzero(input2, sizeof(input2));
            printf("%s\n", funcMenuBuffer);
            printf("请选择你需要的功能：\n");
            fgets(input2, sizeof(input2), stdin);
            sscanf(input2, "%d", &mode);
            if(mode >= 1 && mode <= 5)
            {
                writeData(socketfd, &mode, sizeof(mode));
            }
            /* 清空输入缓冲区 */
            system("clear");

            switch(mode)
            {
            /* 私聊 */
            case PRIVATE_CHAT:
                privateChat(socketfd, loginName);
                break;

            /* 加好友 */
            case FRIEND_ADD:
                
                break;

            /* 群聊 */
            case GROUP_CHAT:
                groupChat(socketfd, loginName);
                break;

            /* 建群 */
            case GROUP_CREATE:
                
                break;

            /* 退出 */
            case INTERNAL_EXIT:
                /* 用户退出函数 */
                flag = userExit(socketfd);
                break;

            default:
                break;
            }
        }
    }
    return 0;
}

/* 用户注册函数 */
static void userRegister(int socketfd)
{
    char username[DEFAULT_LOGIN_NAME];
    bzero(username, sizeof(username));
    char password[DEFAULT_LOGIN_PAWD];
    bzero(password, sizeof(password));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));

    printf("请输入用户名：");
    scanf("%s", username);
    printf("请输入密码：");
    scanf("%s", password);

    /* 发送注册信息给服务器 */
    writeMessage(socketfd, username, sizeof(username) - 1);
    writeMessage(socketfd, password, sizeof(password) - 1);

    /* 接收服务器的响应 */
    bzero(response, sizeof(response));
    readMessage(socketfd, response, sizeof(response) - 1);

    /* 解析服务器的响应并进行处理 */
    if(strncmp(response, "注册成功", sizeof("注册成功")) == 0)
    {
        printf("注册成功\n");
    }
    else if(strncmp(response, "注册失败，用户已存在", sizeof("注册失败，用户已存在")) == 0)
    {
        printf("注册失败，用户已存在\n");
    }
    else
    {
        printf("系统出错\n");
        exit(-1);
    }
    /* 创建好友列表 */
    sqlite3 *chatRoomDB = NULL;
    char friendlistname[BUFFER_SIZE];
    bzero(friendlistname, sizeof(friendlistname));
    strncat(friendlistname, username, sizeof(username));
    strncat(friendlistname, "FriendList.db", sizeof("FriendList.db"));
    char grouplistname[BUFFER_SIZE];
    bzero(grouplistname, sizeof(grouplistname));
    strncat(grouplistname, username, sizeof(username));
    strncat(grouplistname, "GroupList.db", sizeof("GroupList.db"));
    /* 打开数据库：如果数据库不存在，那么就创建 */
    int ret = sqlite3_open(friendlistname, &chatRoomDB);
    if(ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }
    char * ermsg = NULL;
    sprintf(sql, "create table if not exists friendlist (username text not NULL)");
    ret = sqlite3_exec(chatRoomDB, sql, NULL, NULL, &ermsg);
    if(ret != SQLITE_OK)
    {
        printf("sqlite exec error: %s\n", ermsg);
        exit(-1);
    }
    /* 打开数据库：如果数据库不存在，那么就创建 */
    ret = sqlite3_open(grouplistname, &chatRoomDB);
    if(ret != SQLITE_OK)
    {
        perror("sqlite open error");
        exit(-1);
    }
    sprintf(sql, "create table if not exists grouplist (groupname text not NULL)");
    ret = sqlite3_exec(chatRoomDB, sql, NULL, NULL, &ermsg);
    if(ret != SQLITE_OK)
    {
        printf("sqlite exec error: %s\n", ermsg);
        exit(-1);
    }
}

/* 用户私聊函数 */
static int privateChat(int socketfd, char *loginName)
{
    char objectName[DEFAULT_LOGIN_NAME];
    bzero(objectName, sizeof(objectName));
    char sendBuffer[BUFFER_SIZE];
    bzero(sendBuffer, sizeof(sendBuffer));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    
    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    int flag = 0;
    pthread_t recv;

    while(1)
    {
        /* 打开好友列表 */
        char friendList[BUFFER_SIZE];
        bzero(friendList, sizeof(friendList));
        strncpy(friendList, loginName, sizeof(loginName) - 1);
        strncat(friendList, "FriendList.db", strlen("FriendList.db") + 1);
        /* 打开数据库 */
        int ret = sqlite3_open(friendList, &chatRoomDB);
        if(ret != SQLITE_OK)
        {
            perror("sqlite open error");
            exit(-1);
        }
        sprintf(sql, "select * from friendlist");
        char **result = NULL;
        int row = 0;
        sqlite3_get_table(chatRoomDB, sql, &result, &row, NULL, &ermsg);
        if(ret != SQLITE_OK)
        {
            perror("sqlite get table error");
            exit(-1);
        }
        /* 打印好友列表 */
        printf("好友列表\n");
        for(int idx = 1; idx <= row; idx++)
        {
            printf("%s\n", result[idx]);
        }
        printf("----\n");
        /* 选择好友 */
        bzero(objectName, sizeof(objectName));
        printf("请输入私聊对象：\n");
        scanf("%s", objectName);
        if(strncmp(objectName, "q", sizeof(objectName)) == 0)
        {
            writeMessage(socketfd, objectName, strlen(objectName));
            system("clear");
            break;
        }
        /* 检查对象是否在好友列表中 */
        int found = 0;
        sprintf(sql, "select * from friendlist where username = '%s'", objectName);
        ret = sqlite3_exec(chatRoomDB, sql, callback, &found, &ermsg);
        if(ret != SQLITE_OK)
        {
            perror("sqlite open error");
            exit(-1);
        }
        if(found == 0)
        {
            /* 对象不存在 */
            writeMessage(socketfd, "该对象非好友", strlen("该对象非好友"));
            printf("该对象非好友\n");
            sleep(1);
            system("clear");
        }
        else if(found == 1)
        {
            /* 对象存在 */
            /* 发送私聊对象名称给服务器 */
            writeMessage(socketfd, objectName, strlen(objectName));
            /* 接收服务器的响应 */
            bzero(response, sizeof(response));
            readMessage(socketfd, response, sizeof(response) - 1);

            /* 解析服务器的响应并进行处理 */
            if(strncmp(response, "对方在线", sizeof("对方在线")) == 0)
            {
                printf("对方在线\n");
                flag = 1;
                sleep(1);
                system("clear");

                pthread_create(&recv, NULL, recvMessage, (void *)&socketfd);
                char sendCompleteBuffer[BUFFER_SIZE + 2 * DEFAULT_LOGIN_NAME];
                bzero(sendCompleteBuffer, sizeof(sendCompleteBuffer));
                
                while(flag)
                {
                    bzero(sendBuffer, sizeof(sendBuffer));
                    bzero(sendCompleteBuffer, sizeof(sendCompleteBuffer));
                    char c = '0';
                    scanf("%s", sendBuffer);
                    while ((c = getchar()) != EOF && c != '\n');
                    if(strncmp(sendBuffer, "q", sizeof(sendBuffer)) == 0)
                    {
                        writeMessage(socketfd, sendBuffer, strlen(sendBuffer));
                        flag = 0;
                        system("clear");
                        break;
                    }
                    else
                    {
                        sprintf(sendCompleteBuffer, "%s: %s", loginName, sendBuffer);
                        writeMessage(socketfd, sendCompleteBuffer, strlen(sendCompleteBuffer));
                    }
                }
            }
            else if(strncmp(response, "对方不在线", sizeof("对方不在线")) == 0)
            {
                printf("对方不在线\n");
                flag = 0;
                sleep(1);
                system("clear");
            }
        }
        /* 关闭数据库 */
        sqlite3_close(chatRoomDB);
    }
}

/* 用户群聊函数 */
static int groupChat(int socketfd, char *loginName)
{
    char groupName[DEFAULT_LOGIN_NAME];
    bzero(groupName, sizeof(groupName));
    char sendBuffer[BUFFER_SIZE];
    bzero(sendBuffer, sizeof(sendBuffer));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    
    sqlite3 * chatRoomDB = NULL;
    char * ermsg = NULL;
    char sql[BUFFER_SQL];
    bzero(sql, sizeof(sql));
    int flag = 0;
    pthread_t recv;
    
    char groupList[BUFFER_SIZE];
    bzero(groupList, sizeof(groupList));
    strncpy(groupList, loginName, sizeof(loginName) - 1);
    strncat(groupList, "GroupList.db", strlen("GroupList.db") + 1);

    while(1)
    {
        /* 打开群列表 */
        /* 打开数据库 */
        int ret = sqlite3_open(groupList, &chatRoomDB);
        if(ret != SQLITE_OK)
        {
            perror("sqlite open error");
            exit(-1);
        }
        sprintf(sql, "select * from groupList");
        char **result = NULL;
        int row = 0;
        sqlite3_get_table(chatRoomDB, sql, &result, &row, NULL, &ermsg);
        if(ret != SQLITE_OK)
        {
            perror("sqlite get table error");
            exit(-1);
        }
        /* 打印群列表 */
        printf("群列表\n");
        for(int idx = 1; idx <= row; idx++)
        {
            printf("%s\n", result[idx]);
        }
        printf("----\n");
        /* 选择群 */
        bzero(groupName, sizeof(groupName));
        printf("请选择群：\n");
        scanf("%s", groupName);
        if(strncmp(groupName, "q", sizeof(groupName)) == 0)
        {
            writeMessage(socketfd, groupName, strlen(groupName));
            system("clear");
            break;
        }
        /* 检查对象是否在群列表中 */
        int found = 0;
        sprintf(sql, "select * from grouplist where groupname = '%s'", groupName);
        ret = sqlite3_exec(chatRoomDB, sql, callback, &found, &ermsg);
        if(ret != SQLITE_OK)
        {
            perror("sqlite open error");
            exit(-1);
        }
        if(found == 0)
        {
            /* 群不存在 */
            writeMessage(socketfd, "此群未创建", strlen("此群未创建"));
            /* 接收服务器的响应 */
            bzero(response, sizeof(response));
            readMessage(socketfd, response, sizeof(response) - 1);
            printf("此群未创建\n");
            sleep(1);
            system("clear");
        }
        else if(found == 1)
        {
            /* 群存在 */
            writeMessage(socketfd, groupName, strlen(groupName));
            /* 接收服务器的响应 */
            bzero(response, sizeof(response));
            readMessage(socketfd, response, sizeof(response) - 1);

            /* 解析服务器的响应并进行处理 */
            if(strncmp(response, "进入群聊", sizeof("进入群聊")) == 0)
            {
                printf("进入群聊\n");
                flag = 1;
                sleep(1);
                system("clear");

                pthread_create(&recv, NULL, recvMessage, (void *)&socketfd);
                char sendCompleteBuffer[BUFFER_SIZE + 2 * DEFAULT_LOGIN_NAME];
                bzero(sendCompleteBuffer, sizeof(sendCompleteBuffer));
                
                while(flag)
                {
                    char c = '0';
                    scanf("%s", sendBuffer);
                    while ((c = getchar()) != EOF && c != '\n');
                    if(strncmp(sendBuffer, "q", sizeof(sendBuffer)) == 0)
                    {
                        writeMessage(socketfd, sendBuffer, strlen(sendBuffer));
                        flag = 0;
                        system("clear");
                        break;
                    }
                    else
                    {
                        bzero(sendCompleteBuffer, sizeof(sendCompleteBuffer));
                        sprintf(sendCompleteBuffer, "%s: %s", loginName, sendBuffer);
                        writeMessage(socketfd, sendCompleteBuffer, strlen(sendCompleteBuffer));
                    }
                }
            }
            else if(strncmp(response, "进入群聊失败", sizeof("进入群聊失败")) == 0)
            {
                printf("进入群聊失败\n");
                flag = 0;
                sleep(1);
                system("clear");
            }
        }
        /* 关闭数据库 */
        sqlite3_close(chatRoomDB);
    }
}

/* 用户登录函数 */
static int userLogin(int socketfd, char *loginName)
{
    int flag = 0;
    char username[DEFAULT_LOGIN_NAME];
    bzero(username, sizeof(username));
    char password[DEFAULT_LOGIN_PAWD];
    bzero(password, sizeof(password));
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));

    printf("请输入用户名：");
    scanf("%s", username);
    printf("请输入密码：");
    scanf("%s", password);

    /* 发送登录信息给服务器 */
    writeMessage(socketfd, username, sizeof(username) - 1);
    writeMessage(socketfd, password, sizeof(password) - 1);

    /* 接收服务器的响应 */
    bzero(response, sizeof(response));
    readMessage(socketfd, response, sizeof(response) - 1);

    /* 解析服务器的响应并进行处理 */
    if(strncmp(response, "登录成功", sizeof("登录成功")) == 0)
    {
        printf("登录成功\n");
        strncpy(loginName, username, sizeof(username));
        flag = 1;
    }
    else if(strncmp(response, "用户已在别处登录", sizeof("用户已在别处登录")) == 0)
    {
        printf("用户已在别处登录\n");
    }
    else if(strncmp(response, "密码错误", sizeof("密码错误")) == 0)
    {
        printf("密码错误\n");
    }
    else if(strncmp(response, "用户不存在", sizeof("用户不存在")) == 0)
    {
        printf("用户不存在\n");
    }
    else
    {
        printf("系统出错\n");
        exit(-1);
    }
    return flag;
}

/* 客户端退出函数 */
static void clientExit(int socketfd, int mainMenufd, int funcMenufd)
{
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    /* 接收服务器的响应 */
    bzero(response, sizeof(response));
    readMessage(socketfd, response, sizeof(response) - 1);

    /* 解析服务器的响应并进行处理 */
    if(strncmp(response, "客户端退出", sizeof("客户端退出")) == 0)
    {
        printf("客户端退出\n");
        sleep(1);
        system("clear");
        close(mainMenufd);
        close(funcMenufd);
        exit(-1);
    }
    else
    {
        printf("客户端退出失败\n");
    }
}

/* 用户退出函数 */
static int userExit(int socketfd)
{
    int flag = 1;
    char response[BUFFER_SIZE];
    bzero(response, sizeof(response));
    /* 接收服务器的响应 */
    bzero(response, sizeof(response));
    readMessage(socketfd, response, sizeof(response) - 1);

    /* 解析服务器的响应并进行处理 */
    if(strncmp(response, "用户退出", sizeof("用户退出")) == 0)
    {
        printf("用户退出\n");
        sleep(1);
        system("clear");
        flag = 0;
    }
    else
    {
        printf("用户退出失败\n");
        sleep(1);
        system("clear");
    }
    return flag;
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
static void writeData(int fd, int *data, int dataSize)
{
    int ret = write(fd, data, dataSize);
    if (ret < 0)
    {
        perror("write error");
        close(fd);
        exit(-1);
    }
}
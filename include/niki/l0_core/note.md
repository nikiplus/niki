为了理解解释器是如何工作的，首先需要想清楚代码是什么。
我们的代码到计算机可执行的指令，图可简单绘制成如下这样一套

文本(TEXT)
 ↓
指令(OPCODE)
 ↓
执行(ACTION)

while（true）{
    switch TEXT
    case TEXT1->OPCODE2;
    case TEXT2->OPCODE2
    case .....

    run(OPCODE)
    case OPCODE1->ACTION1;
    case OPCODE2->ACTION2;
    case...
}

以上面模式拆分，我们可将模块拆分为：
1)扫描（scanner）-> 2)执行(vm::run)

然而文本到指令之间有一条鸿沟，即，计算机无法识别文本本身。
因此我们需要对文本进行规范和定义，我们将这种被标识过的文本称为token

文本(TEXT)
 ↓
翻译(trans to Token)
 ↓
指令(OPCODE)
 ↓
执行(ACTION)

while（true）{
    switch TEXT
    case TEXT1->trans to Token1;
    case TEXT2->trans to Token2;
    case .....

    switch OPCODE
    case TOKEN1->OPCODE1;
    case TOKEN2->OPCODE2;
    case .....

    run(OPCODE)
    case OPCODE1->ACTION1;
    case OPCODE2->ACTION2;
    case...
}

那么模块拆分就变成了：
1)扫描（scanner）-> 2)翻译(compiler) -> 3)执行(vm::run)

好，那么我们现在可以从这三个模块着手，开始深入。

在此之前，假设我们面对这样一段文本：

var test :int = 10+8*（1-2）;

在面对这段文本时，我们有如下几个功能需要解决：
0) 计算机如何识别 var、int 等关键字？如何将test视为一个变量名？如何将10视为一个数字？将=视为赋值运算符号？
1) 如何识别var，使var之后的"test"字符正确被识别为一个变量？
2) 如何将test这个var被标记为int？并进行正确的符合int定义的计算？
3) 如何让计算机在读到 "="之后，把10这个值赋给test？
4) var test:int 的这个值存在哪里？
5) 计算机如何知道";"意味着一段句子的结尾？":"应该在什么之前，在什么之后？
6) 如何让10+8*（1-2）能够正确运算，即，先计算1-2，再计算*8，最后计算10+？


这几个问题事实上是我们解释器开发中相当核心的几个问题，我们现在不急着将其全部解决，而是先抓住其中一小部分，联合实际模块进行深入。

首先来梳理识别相关问题，想要让文本有意义，那么文本的识别就是第一步，因此我们先来看扫描（scanner）相关的问题。
事实上，scanner的功能相对简单，因为它所需要的输入和输出都相对好理解，

输入_无意义文本(text)
    ↓
扫描(scanner)
    ↓
输出_有意义符号

那么我们要做的第一步就是，定义哪些文本是有意义的，还是以我们上面那段代码举例：var test :int = 10+8*（1-2）;

文本：
| var | space | test | space | : | int | space | = | space | 10 | + | 8 | * | ( | 1 | - | 2 | ) | ; |

嗯……除了空格（space）以外全都有意义，那么我们就先拿var来举例，scanner怎么能知道v a r这三个字母有意义呢？
——scanner当然不知道，它需要我们提供一个字典。

enum 字典(TOKEN_TABLE){
    有意义文本(meanful text):Token:"var";
    无意义文本(meanless test):Token:"space";
    有意义文本(meanful text):Token:"test";
    有意义文本(meanful text):Token:":";
    有意义文本(meanful text):Token:"int";
    ...num token:10,token:8,token:1,token:2;
    ...op token:+,token:*,token:-;token:;,token:(,token:)
}

那么scanner就可以用扫描来的文字和字典进行一一对应，把读到哪段文本，就发射对应的token——起码目前看来是这样不是吗？
首先我们需要一个指针来指向文本，好和我们的字典进行比对。

指针 当前所指（current）=文本[0];//默认指针指向初始位

while(true){
    swith(当前所指){
    case 当前所指 = “var” : 这是一个 Token:var;
    case 当前所指= "space":这是一个 Token：space;
    case 当前所指= “test” : 这是一个 Token:test;
    case 当前所指= ":" : 这是一个 Token:":";
    case 当前所指= "int" : 这是一个 Token:int;
    ...是一个token:10;token:8;token:1;token:2;
    op token:+;token:*;token:-;token:;,token:(,token:)}

    当前所指++;; 往前一位
}

看出问题了吗？spcae是token，所有的数字都单独有token，甚至test也是token？
我们在实际编程的时候，会输入多种不同数字，那么绝不可能将每个数字就视为一个token，而应该建立一个统一的数字token。
-token：num;//这样只要遇到数字，我们就将其标记为num。
同理，我们会面对许多类似“test”这样的变量名和函数名，因此我们也可以将这部分数据视为一个token
-token：IDENTIFIER(标识符);//这样一旦遇到类似test的，就把它看作标识符。

那么空格呢？事实上，空格不需要解析，我们看到空格时只需要直接跳过，去看下一个字符就可以了。
那么字典就变成了。

enum 字典(TOKEN_TABLE){
Token:var；
Token:冒号；
Token:int;
Token:num;
...Token:+/-/*/(/)/=/;
}



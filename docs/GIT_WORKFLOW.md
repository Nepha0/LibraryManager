# 四人 Git 协作操作指南

Git 保存本地版本，GitHub 保存共享远程仓库。main 是稳定版本；feature 分支是单次任务的工作副本；PR 是请求团队检查并合并代码。
下列命令中的仓库地址、姓名、邮箱和分支名需要按实际情况填写。命令逐行运行，有错误先处理再继续。

## 组长首次准备远程

GitHub 新建私有仓库 LibraryManager，若已有本地提交，不勾选自动创建 README、License 或 .gitignore。
在仓库 Settings → Collaborators 中邀请另外三人的 GitHub 账号，成员接受邀请后才能访问私有仓库。
若助手已建立并推送仓库，跳过新建及重复初始化步骤，只检查 `git remote -v`。

```powershell
cd 'E:\shixun\QT大作业\LibraryManager'
git remote add origin https://github.com/ZZ8Z9/LibraryManager.git
git push -u origin main
```

已存在 origin 时先运行 `git remote -v` 检查，不重复 add，不随意覆盖原有地址。
在 Settings → Rules / Branches 中，如果账号套餐支持私有仓库分支保护，要求 PR 和至少一人审查并禁止强推；若不支持，按团队约定执行。

## 每个成员第一次操作

安装 Git for Windows；GitHub 登录使用浏览器或凭据管理器，不把令牌发到群里，也不写入远程 URL。
在自己的电脑选择一个存放工程的目录，然后克隆；不要先新建另一个 Qt 工程。

```powershell
git clone https://github.com/ZZ8Z9/LibraryManager.git
cd LibraryManager
git config user.name "你的姓名或 GitHub 昵称"
git config user.email "GitHub 已验证邮箱或 GitHub 提供的 noreply 邮箱"
git status
```

以上身份仅设置当前仓库，不改变其他项目。Qt Creator 打开克隆目录中的 LibraryManager.pro。

## 每次开始新任务

先确保 `git status` 没有未保存改动。有改动先在原任务分支提交，不盲目切换。

```powershell
git switch main
git pull --ff-only origin main
git switch -c feature/books-search
```

示例任务分支：`feature/auth-login`、`feature/books-search`、`feature/loans-return`、`feature/readers-edit`。
每个分支解决一项明确任务，完成后重新从最新 main 创建下一个分支。

## 保存并上传自己的修改

```powershell
git status
git diff
git add books/
git diff --cached
git commit -m "feat: add book search"
git push -u origin feature/books-search
```

`git add books/` 仅为 B 模块示例；将路径换成自己的具体文件或目录。
共同检查暂存差异，避免误提交他人的改动、密码及数据文件。
首次 push 可能弹出 GitHub 浏览器登录；GitHub 网站登录不代表命令行已经登录。

## 提 PR 并合并

1. push 后打开 GitHub 仓库，点击 Compare & pull request。
2. base 选择 main，compare 选择自己的任务分支。
3. 填写修改功能、运行验证和数据库影响，请另一位成员检查。
4. 审查者拉取该分支构建运行，通过后由 A 或约定的负责人合并。
5. 合并后其他成员更新 main，禁止四个人直接往 main 堆代码。

```powershell
git switch main
git pull --ff-only origin main
```

之后为新任务创建新分支。GitHub 可在 PR 合并后删除远程任务分支。

## 开发过程中同步 main

在自己的任务分支提交或妥善保存当前改动后：

```powershell
git fetch origin
git merge origin/main
```

若出现冲突，使用编辑器处理 `<<<<<<<`、`=======`、`>>>>>>>` 标记，和相关成员确认正确内容。
修改后执行 `git add 发生冲突的文件`、`git commit`，再构建验证并 push。
如果暂时无法处理，可用 `git merge --abort` 退出此次合并，保留合并前状态。
不要用 `git reset --hard`、强推或整份覆盖文件来解决不理解的冲突。

## 四条协作约定

- 不互传 zip 当作版本同步；使用 GitHub 拉取和推送。
- 不提交 build、Qt 的 .pro.user、本地数据库、密码及安装器。
- `.pro`、公共接口和建表脚本由 A 协调，单个任务尽量只改自己的模块。
- 每个 PR 都能说明做了什么、如何验证；Git 提交不能代替真实组会纪要和报告。

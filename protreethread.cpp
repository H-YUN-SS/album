#include "protreethread.h"
#include <QDir>
#include "protreeitem.h"
#include "const.h"
ProTreeThread::ProTreeThread(const QString &src_path,
                             const QString &dist_path,
                             QTreeWidgetItem *parent_item,
                             int file_count,
                             QTreeWidget *self,
                             QTreeWidgetItem *root,
                             QObject *parent)
    :QThread(parent),_src_path(src_path),_dist_path(dist_path),
    _file_count(file_count),_parent_item(parent_item),
    _self(self),_root(root),_bstop(false)
{

}

ProTreeThread::~ProTreeThread()
{

}

void ProTreeThread::run()
{
    CreateProTree(_src_path,_dist_path,_parent_item,_file_count,_self,_root);
    if(_bstop)
    {
        auto path = dynamic_cast<ProTreeItem*>(_root)->GetPath();
        auto index = _self->indexOfTopLevelItem(_root);
        delete _self->takeTopLevelItem(index);
        QDir dir(path);
        dir.removeRecursively();
        return;
    }
    emit SigFinishProgress(_file_count);
}

void ProTreeThread::CreateProTree(const QString &src_path, const QString &dist_path, QTreeWidgetItem *parent_item, int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem *preItem)
{
    if(_bstop)
    {
        return;
    }
    bool needcopy = true;
    if(src_path==dist_path)
    {
        needcopy=false;
    }
    QDir import_dir(src_path);
    QStringList nameFilters;
    import_dir.setFilter(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot);
    import_dir.setSorting(QDir::Name);
    QFileInfoList list = import_dir.entryInfoList();

    for(int i=0;i<list.size();i++)
    {
        if(_bstop)
        {
            return;
        }
        QFileInfo fileInfo = list.at(i);
        bool bIsDir = fileInfo.isDir();
        if(bIsDir)
        {
            if(_bstop)
            {
                return;
            }
            file_count++;
            emit SigUpdateProgress(file_count);

            QDir dist_dir(_dist_path);
            QString sub_dist_path = dist_dir.absoluteFilePath(fileInfo.fileName());
            QDir sub_dist_dir(sub_dist_path);
            if(!sub_dist_dir.exists())
            {
                bool ok = sub_dist_dir.mkpath(sub_dist_path);
                if(!ok)
                {
                    continue;
                }
            }
            auto * item = new ProTreeItem(parent_item,fileInfo.fileName(),sub_dist_path,root,TreeItemDir);
            item->setData(0,Qt::DisplayRole,fileInfo.fileName());
            item->setData(0,Qt::DecorationRole,QIcon(":/icon/dir.png"));
            item->setData(0,Qt::ToolTipRole,sub_dist_path);

            CreateProTree(fileInfo.absoluteFilePath(),sub_dist_path,item,file_count,self,root,preItem);
        }
        else
        {
            if(_bstop)
            {
                return;
            }

            const QString& suffix = fileInfo.completeSuffix();
            if(suffix!="png"&&suffix!="jpeg"&&suffix!="jpg")
            {
                continue;
            }
            file_count++;
            emit SigUpdateProgress(file_count);
            if(!needcopy)
            {
                continue;
            }
            QDir dist_dir(dist_path);
            QString dist_file_path=dist_dir.absoluteFilePath(fileInfo.fileName());
            if(!QFile::copy(fileInfo.absoluteFilePath(),dist_file_path))
            {
                continue;
            }
            auto * item = new ProTreeItem(parent_item,fileInfo.fileName(),dist_file_path,root,TreeItemPic);
            item->setData(0,Qt::DisplayRole,fileInfo.fileName());
            item->setData(0,Qt::DecorationRole,QIcon(":/icon/pic.png"));
            item->setData(0,Qt::ToolTipRole,dist_file_path);

            if(preItem)
            {
                auto * pre_proitem = dynamic_cast<ProTreeItem*>(preItem);
                pre_proitem->SetNextItem(item);
            }
            item->SetPreItem(preItem);
            preItem = item;
        }
    }
}

void ProTreeThread::SlotCancelProgress()
{
    this->_bstop = true;
}

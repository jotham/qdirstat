/*
 *   File name: TreeWalker.h
 *   Summary:	QDirStat helper class to walk a FileInfo tree
 *   License:	GPL V2 - See file LICENSE for details.
 *
 *   Author:	Stefan Hundhammer <Stefan.Hundhammer@gmx.de>
 */


#ifndef TreeWalker_h
#define TreeWalker_h

#include "FileInfo.h"


namespace QDirStat
{
    /**
     * Abstract base class to walk recursively through a FileInfo tree to check
     * for each tree item whether or not it should be used for further
     * processing.
     *
     * This is used in the actions in the "discover" menu to check if items fit
     * into a certain category like
     *   - largest files
     *   - newest files
     *   - oldest files
     *   - files with multiple hard links
     *   - broken symlinks
     *   - sparse files
     **/
    class TreeWalker
    {
    public:

        TreeWalker() {}
        virtual ~TreeWalker() {}

        /**
         * General preparations before items are checked. This can be used to
         * calculate thresholds for later checks, e.g. up to which value an
         * item is considered to belong to the category. This may involve
         * traversing the tree a first time to calculate that value, e.g. by
         * adding all appropriate items to an internal list that is sorted so
         * the value of the nth first or last element is used.
         *
         * This default implementation does nothing.
         **/
        virtual void prepare( FileInfo * /* subtree */ ) {}

        /**
         * Check if 'item' fits into the category (largest / newest / oldest
         * file etc.). Return 'true' if it fits, 'false' if not.
         *
         * Derived classes are required to implement this.
         **/
        virtual bool check( FileInfo * item ) = 0;

    };  // class TreeWalker


    /**
     * TreeWalker to find the largest files.
     **/
    class LargestFilesTreeWalker: public TreeWalker
    {
    public:

        /**
         * Find the threshold for what is considered a "large file".
         **/
        virtual void prepare( FileInfo * subtree );

        virtual bool check( FileInfo * item )
            { return item && item->isFile() && item->size() >= _threshold; }

    protected:

        FileSize _threshold;
    };


    /**
     * TreeWalker to find new files.
     **/
    class NewFilesTreeWalker: public TreeWalker
    {
    public:

        /**
         * Find the threshold for what is considered a "new file".
         **/
        virtual void prepare( FileInfo * subtree );

        virtual bool check( FileInfo * item )
            { return item && item->isFile() && item->mtime() >= _threshold; }

    protected:

        time_t _threshold;
    };


    /**
     * TreeWalker to find old files.
     **/
    class OldFilesTreeWalker: public TreeWalker
    {
    public:

        /**
         * Find the threshold for what is considered an "old file".
         **/
        virtual void prepare( FileInfo * subtree );

        virtual bool check( FileInfo * item )
            { return item && item->isFile() && item->mtime() <= _threshold; }

    protected:

        time_t _threshold;
    };


    /**
     * TreeWalker to find files with multiple hard links.
     **/
    class HardLinkedFilesTreeWalker: public TreeWalker
    {
    public:

        virtual bool check( FileInfo * item )
            { return item && item->isFile() && item->links() > 1; }
    };


    /**
     * TreeWalker to find broken symlinks.
     **/
    class BrokenSymLinksTreeWalker: public TreeWalker
    {
    public:

        virtual bool check( FileInfo * item );
    };


    /**
     * TreeWalker to find sparse files.
     **/
    class SparseFilesTreeWalker: public TreeWalker
    {
    public:

        virtual bool check( FileInfo * item )
            { return item && item->isFile() && item->isSparseFile(); }
    };

}       // namespace QDirStat

#endif  // TreeWalker_h

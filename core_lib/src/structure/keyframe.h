/*

Pencil2D - Traditional Animation Software
Copyright (C) 2005-2007 Patrick Corrieri & Pascal Naidon
Copyright (C) 2012-2020 Matthew Chiawen Chang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

#ifndef KeyFrame_H
#define KeyFrame_H

#include <cstdint>
#include <vector>
#include <memory>
#include <QString>
#include "pencilerror.h"
class KeyFrameEventListener;


class KeyFrame
{
public:
    explicit KeyFrame();
    explicit KeyFrame(const KeyFrame& k2);
    virtual ~KeyFrame();

    KeyFrame& operator=(const KeyFrame& k2);

    int  pos() const { return mFrame; }
    void setPos(int position) { mFrame = position; }

    int length() const { return mLength; }
    void setLength(int len) { mLength = len; }

    void modification() { mIsModified = true; }
    void setModified(bool b) { mIsModified = b; }
    bool isModified() const { return mIsModified; }

    QString fileName() const { return mAttachedFileName; }
    void    setFileName(QString strFileName) { mAttachedFileName = strFileName; }

    void addEventListener(KeyFrameEventListener*);
    void removeEventListner(KeyFrameEventListener*);

    virtual KeyFrame* clone() const { return nullptr; }
    virtual void loadFile() {}
    virtual void unloadFile() {}
    virtual bool isLoaded() const { return true; }

    virtual quint64 memoryUsage() { return 0; }

private:
    int mFrame = -1;
    int mLength = 1;
    bool mIsModified = true;
    QString mAttachedFileName;

    std::vector<KeyFrameEventListener*> mEventListeners;
};

class KeyFrameEventListener
{
public:
    virtual void onKeyFrameDestroy(KeyFrame*) = 0;
};

#endif // KeyFrame_H

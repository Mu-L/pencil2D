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

#include "keyframe.h"


KeyFrame::KeyFrame()
{
}

KeyFrame::KeyFrame(const KeyFrame& k2)
{
    mFrame = k2.mFrame;
    mLength = k2.mLength;
    mIsModified = k2.mIsModified;
    mAttachedFileName = k2.mAttachedFileName;
    // intentionally not copying event listeners
}

KeyFrame::~KeyFrame()
{
    for (KeyFrameEventListener* listener : mEventListeners)
    {
        listener->onKeyFrameDestroy(this);
    }
}

KeyFrame& KeyFrame::operator=(const KeyFrame& k2)
{
	if (this == &k2)
	{
		return *this; // a self-assignment
	}

	mFrame = k2.mFrame;
	mLength = k2.mLength;
	mIsModified = k2.mIsModified;
	mAttachedFileName = k2.mAttachedFileName;
    // intentionally not copying event listeners
    return *this;
}

void KeyFrame::addEventListener(KeyFrameEventListener* listener)
{
    auto it = std::find(mEventListeners.begin(), mEventListeners.end(), listener);
    if (it == mEventListeners.end())
    {
        mEventListeners.push_back(listener);
    }
}

void KeyFrame::removeEventListner(KeyFrameEventListener* listener)
{
    auto it = std::find(mEventListeners.begin(), mEventListeners.end(), listener);
    if (it != mEventListeners.end())
    {
        mEventListeners.erase(it);
    }
}

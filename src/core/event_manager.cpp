#include "core/event_manager.h"


namespace Lux
{


void EventManager::emitEvent(Event& event)
{
	ListenerMap::iterator iter = m_listeners.find(event.getType());
	if(iter != m_listeners.end())
	{
		for(int i = 0, c = iter.second().size(); i < c; ++i)
		{
			iter.second()[i].invoke(event);
		}
	}
}


EventManager::Listener& EventManager::addListener(Event::Type type)
{
	return m_listeners[type].push_back_empty();
}


void EventManager::removeListener(Event::Type type, const Listener& listener)
{
	ASSERT(false); // TODO
}


} // !namespace Lux
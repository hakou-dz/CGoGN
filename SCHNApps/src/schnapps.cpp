#include "schnapps.h"

#include <QVBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <QDockWidget>
#include <QPluginLoader>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "controlDock_cameraTab.h"
#include "controlDock_mapTab.h"
#include "controlDock_pluginTab.h"

#include "camera.h"
#include "view.h"
#include "texture.h"
#include "plugin.h"
#include "plugin_interaction.h"
#include "plugin_processing.h"
#include "mapHandler.h"

namespace CGoGN
{

namespace SCHNApps
{

SCHNApps::SCHNApps(const QString& appPath, PythonQtObjectPtr& pythonContext, PythonQtScriptingConsole& pythonConsole) :
	QMainWindow(),
	m_appPath(appPath),
	m_pythonContext(pythonContext),
	m_pythonConsole(pythonConsole),
	m_firstView(NULL),
	m_selectedView(NULL)
{
	this->setupUi(this);

	// create & setup control dock

	m_controlDock = new QDockWidget("Control Dock", this);
	m_controlDock->setAllowedAreas(Qt::LeftDockWidgetArea);
	m_controlDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

	m_controlDockTabWidget = new QTabWidget(m_controlDock);
	m_controlDockTabWidget->setObjectName("ControlDockTabWidget");
	m_controlDockTabWidget->setLayoutDirection(Qt::LeftToRight);
	m_controlDockTabWidget->setTabPosition(QTabWidget::North);

	addDockWidget(Qt::LeftDockWidgetArea, m_controlDock);
	m_controlDock->setVisible(true);
	m_controlDock->setWidget(m_controlDockTabWidget);

	m_controlCameraTab = new ControlDock_CameraTab(this);
	m_controlDockTabWidget->addTab(m_controlCameraTab, m_controlCameraTab->title());
	m_controlMapTab = new ControlDock_MapTab(this);
	m_controlDockTabWidget->addTab(m_controlMapTab, m_controlMapTab->title());
	m_controlPluginTab = new ControlDock_PluginTab(this);
	m_controlDockTabWidget->addTab(m_controlPluginTab, m_controlPluginTab->title());

	m_controlDockTabWidget->setMaximumWidth(m_controlCameraTab->width());

	connect(action_ShowHideControlDock, SIGNAL(triggered()), this, SLOT(showHideControlDock()));

	// create & setup plugin dock

	m_pluginDock = new QDockWidget("Plugins Dock", this);
	m_pluginDock->setAllowedAreas(Qt::RightDockWidgetArea);
	m_pluginDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

	m_pluginDockTabWidget = new QTabWidget(m_pluginDock);
	m_pluginDockTabWidget->setObjectName("PluginDockTabWidget");
	m_pluginDockTabWidget->setLayoutDirection(Qt::LeftToRight);
	m_pluginDockTabWidget->setTabPosition(QTabWidget::East);

	addDockWidget(Qt::RightDockWidgetArea, m_pluginDock);
	m_pluginDock->setVisible(false);
	m_pluginDock->setWidget(m_pluginDockTabWidget);

	connect(action_ShowHidePluginDock, SIGNAL(triggered()), this, SLOT(showHidePluginDock()));

	// create & setup python dock

	m_pythonDock = new QDockWidget("Python Dock", this);
	m_pythonDock->setAllowedAreas(Qt::BottomDockWidgetArea);
	m_pythonDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

	addDockWidget(Qt::BottomDockWidgetArea, m_pythonDock);
	m_pythonDock->setVisible(false);
	m_pythonDock->setWidget(&m_pythonConsole);

	connect(action_ShowHidePythonDock, SIGNAL(triggered()), this, SLOT(showHidePythonDock()));
	connect(action_LoadPythonScript, SIGNAL(triggered()), this, SLOT(loadPythonScriptFromFileDialog()));

	// create & setup central widget (views)

	glewInit();

	m_centralLayout = new QVBoxLayout(centralwidget);

	m_rootSplitter = new QSplitter(centralwidget);
	b_rootSplitterInitialized = false;
	m_centralLayout->addWidget(m_rootSplitter);

	m_firstView = addView();
	setSelectedView(m_firstView);
	m_rootSplitter->addWidget(m_firstView);

	// connect basic actions

	connect(action_AboutSCHNApps, SIGNAL(triggered()), this, SLOT(aboutSCHNApps()));
	connect(action_AboutCGoGN, SIGNAL(triggered()), this, SLOT(aboutCGoGN()));

	// register a first plugins directory

	registerPluginsDirectory(m_appPath + QString("/../lib"));
}

SCHNApps::~SCHNApps()
{
}

/*********************************************************
 * MANAGE CAMERAS
 *********************************************************/

Camera* SCHNApps::addCamera(const QString& name)
{
	if (m_cameras.contains(name))
		return NULL;

	Camera* camera = new Camera(name, this);
	m_cameras.insert(name, camera);
	emit(cameraAdded(camera));
	return camera;
}

Camera* SCHNApps::addCamera()
{
	return addCamera(QString("camera_") + QString::number(Camera::cameraCount));
}

void SCHNApps::removeCamera(const QString& name)
{
	Camera* camera = getCamera(name);
	if (camera && !camera->isUsed())
	{
		m_cameras.remove(name);
		emit(cameraRemoved(camera));
		delete camera;
	}
}

Camera* SCHNApps::getCamera(const QString& name) const
{
	if (m_cameras.contains(name))
		return m_cameras[name];
	else
		return NULL;
}

/*********************************************************
 * MANAGE VIEWS
 *********************************************************/

View* SCHNApps::addView(const QString& name)
{
	if (m_views.contains(name))
		return NULL;

	View* view = NULL;
	if(m_firstView == NULL)
		view = new View(name, this);
	else
		view = new View(name, this, m_firstView);
	m_views.insert(name, view);

	emit(viewAdded(view));

	return view;
}

View* SCHNApps::addView()
{
	return addView(QString("view_") + QString::number(View::viewCount));
}

void SCHNApps::removeView(const QString& name)
{
	if (m_views.contains(name))
	{
		if(m_views.count() > 1)
		{
			View* view = m_views[name];
			if(view == m_firstView)
			{
				ViewSet::const_iterator i = m_views.constBegin();
				while (i != m_views.constEnd())
				{
					if(i.value() != view)
					{
						m_firstView = i.value();
						i = m_views.constEnd();
					}
					else
						++i;
				}
			}
			if(view == m_selectedView)
				setSelectedView(m_firstView);

			m_views.remove(name);

			emit(viewRemoved(view));

			delete view;
		}
	}
}

View* SCHNApps::getView(const QString& name) const
{
	if (m_views.contains(name))
		return m_views[name];
	else
		return NULL;
}

void SCHNApps::setSelectedView(View* view)
{
	if(m_selectedView)
	{
		foreach(PluginInteraction* p, m_selectedView->getLinkedPlugins())
			disablePluginTabWidgets(p);
		disconnect(m_selectedView, SIGNAL(pluginLinked(PluginInteraction*)), this, SLOT(enablePluginTabWidgets(PluginInteraction*)));
		disconnect(m_selectedView, SIGNAL(pluginUnlinked(PluginInteraction*)), this, SLOT(disablePluginTabWidgets(PluginInteraction*)));
	}

	View* oldSelected = m_selectedView;
	m_selectedView = view;

	foreach(PluginInteraction* p, m_selectedView->getLinkedPlugins())
		enablePluginTabWidgets(p);
	connect(m_selectedView, SIGNAL(pluginLinked(PluginInteraction*)), this, SLOT(enablePluginTabWidgets(PluginInteraction*)));
	connect(m_selectedView, SIGNAL(pluginUnlinked(PluginInteraction*)), this, SLOT(disablePluginTabWidgets(PluginInteraction*)));

	emit(selectedViewChanged(oldSelected, m_selectedView));

	if(oldSelected)
		oldSelected->updateGL();
	m_selectedView->updateGL();
}

void SCHNApps::splitView(const QString& name, Qt::Orientation orientation)
{
	View* newView = addView();

	View* view = m_views[name];
	QSplitter* parent = (QSplitter*)(view->parentWidget());
	if(parent == m_rootSplitter && !b_rootSplitterInitialized)
	{
		m_rootSplitter->setOrientation(orientation);
		b_rootSplitterInitialized = true;
	}
	if(parent->orientation() == orientation)
		parent->insertWidget(parent->indexOf(view)+1, newView);
	else
	{
		int idx = parent->indexOf(view);
		view->setParent(NULL);
		QSplitter* spl = new QSplitter(orientation);
		spl->addWidget(view);
		spl->addWidget(newView);
		parent->insertWidget(idx, spl);
	}
}

/*********************************************************
 * MANAGE PLUGINS
 *********************************************************/

void SCHNApps::registerPluginsDirectory(const QString& path)
{
	QDir directory(path);
	if(directory.exists())
	{
		QStringList filters;
		filters << "lib*.so";
		filters << "lib*.dylib";
		filters << "*.dll";

		QStringList pluginFiles = directory.entryList(filters, QDir::Files);

		foreach(QString pluginFile, pluginFiles)
		{
			QFileInfo pfi(pluginFile);
			QString pluginName = pfi.baseName().remove(0, 3);
			QString pluginFilePath = directory.absoluteFilePath(pluginFile);

			if(!m_availablePlugins.contains(pluginName))
			{
				m_availablePlugins.insert(pluginName, pluginFilePath);
				emit(pluginAvailableAdded(pluginName));
			}
		}
	}
}

Plugin* SCHNApps::enablePlugin(const QString& pluginName)
{
	if (m_plugins.contains(pluginName))
		return m_plugins[pluginName];

	if (m_availablePlugins.contains(pluginName))
	{
		QString pluginFilePath = m_availablePlugins[pluginName];

		QPluginLoader loader(pluginFilePath);

		// if the loader loads a plugin instance
		if (QObject* pluginObject = loader.instance())
		{
			Plugin* plugin = qobject_cast<Plugin*>(pluginObject);

			// set the plugin with correct parameters (name, filepath, SCHNApps)
			plugin->setName(pluginName);
			plugin->setFilePath(pluginFilePath);
			plugin->setSCHNApps(this);

			// then we call its enable() methods
			if (plugin->enable())
			{
				// if it succeeded we reference this plugin
				m_plugins.insert(pluginName, plugin);

				statusbar->showMessage(pluginName + QString(" successfully loaded."), 2000);
				emit(pluginEnabled(plugin));

				// method success
				return plugin;
			}
			else
			{
				delete plugin;
				return NULL;
			}
		}
		// if loading fails
		else
		{
			return NULL;
		}
	}
	else
	{
		return NULL;
	}
}

void SCHNApps::disablePlugin(const QString& pluginName)
{
	if (m_plugins.contains(pluginName))
	{
		Plugin* plugin = m_plugins[pluginName];

		// remove plugin dock tabs
		foreach(QWidget* tab, m_pluginTabs[plugin])
			removePluginDockTab(plugin, tab);
		// remove plugin menu actions
		foreach(QAction* action, m_pluginMenuActions[plugin])
			removeMenuAction(plugin, action);

		// unlink linked views (for interaction plugins)
		PluginInteraction* pi = dynamic_cast<PluginInteraction*>(plugin);
		if(pi)
		{
			foreach(View* view, pi->getLinkedViews())
				view->unlinkPlugin(pi);
		}

		// call disable() method and dereference plugin
		plugin->disable();
		m_plugins.remove(pluginName);

		QPluginLoader loader(plugin->getFilePath());
		loader.unload();

		statusbar->showMessage(pluginName + QString(" successfully unloaded."), 2000);
		emit(pluginDisabled(plugin));

		delete plugin;
	}
}

Plugin* SCHNApps::getPlugin(const QString& name) const
{
	if (m_plugins.contains(name))
		return m_plugins[name];
	else
		return NULL;
}

void SCHNApps::addPluginDockTab(Plugin* plugin, QWidget* tabWidget, const QString& tabText)
{
	if(tabWidget && !m_pluginTabs[plugin].contains(tabWidget))
	{
		int currentTab = m_pluginDockTabWidget->currentIndex();

		int idx = m_pluginDockTabWidget->addTab(tabWidget, tabText);
		m_pluginDock->setVisible(true);

		PluginInteraction* pi = dynamic_cast<PluginInteraction*>(plugin);
		if(pi)
		{
			if(pi->isLinkedToView(m_selectedView))
				m_pluginDockTabWidget->setTabEnabled(idx, true);
			else
				m_pluginDockTabWidget->setTabEnabled(idx, false);
		}

		if(currentTab != -1)
			m_pluginDockTabWidget->setCurrentIndex(currentTab);

		m_pluginTabs[plugin].append(tabWidget);
	}
}

void SCHNApps::removePluginDockTab(Plugin* plugin, QWidget *tabWidget)
{
	if(tabWidget && m_pluginTabs[plugin].contains(tabWidget))
	{
		m_pluginDockTabWidget->removeTab(m_pluginDockTabWidget->indexOf(tabWidget));

		m_pluginTabs[plugin].removeOne(tabWidget);
	}
}

void SCHNApps::enablePluginTabWidgets(PluginInteraction* plugin)
{
//	int currentTab = m_dockTabWidget->currentIndex();

	if(m_pluginTabs.contains(plugin))
	{
		foreach(QWidget* w, m_pluginTabs[plugin])
			m_pluginDockTabWidget->setTabEnabled(m_pluginDockTabWidget->indexOf(w), true);
	}

//	m_dockTabWidget->setCurrentIndex(currentTab);
}

void SCHNApps::disablePluginTabWidgets(PluginInteraction* plugin)
{
//	int currentTab = m_dockTabWidget->currentIndex();

	if(m_pluginTabs.contains(plugin))
	{
		foreach(QWidget* w, m_pluginTabs[plugin])
			m_pluginDockTabWidget->setTabEnabled(m_pluginDockTabWidget->indexOf(w), false);
	}

//	m_dockTabWidget->setCurrentIndex(currentTab);
}

/*********************************************************
 * MANAGE MAPS
 *********************************************************/

MapHandlerGen* SCHNApps::addMap(const QString& name, unsigned int dim)
{
	if (m_maps.contains(name))
		return NULL;

	MapHandlerGen* mh = NULL;
	switch(dim)
	{
		case 2 : {
			PFP2::MAP* map = new PFP2::MAP();
			mh = new MapHandler<PFP2>(name, this, map);
			break;
		}
		case 3 : {
			PFP3::MAP* map = new PFP3::MAP();
			mh = new MapHandler<PFP3>(name, this, map);
			break;
		}
	}

	m_maps.insert(name, mh);

	emit(mapAdded(mh));

	return mh;
}

void SCHNApps::removeMap(const QString& name)
{
	if (m_maps.contains(name))
	{
		MapHandlerGen* map = m_maps[name];

		foreach(View* view, map->getLinkedViews())
			view->unlinkMap(map);

		m_maps.remove(name);

		emit(mapRemoved(map));

		delete map;
	}
}

MapHandlerGen* SCHNApps::getMap(const QString& name) const
{
	if (m_maps.contains(name))
		return m_maps[name];
	else
		return NULL;
}

MapHandlerGen* SCHNApps::getSelectedMap() const
{
	return m_controlMapTab->getSelectedMap();
}

/*********************************************************
 * MANAGE TEXTURES
 *********************************************************/

Texture* SCHNApps::getTexture(const QString& image)
{
	if(m_textures.contains(image))
	{
		Texture* t = m_textures[image];
		t->ref++;
		return t;
	}
	else
	{
		Texture* t = NULL;
		QImage img(image);
		if(!img.isNull())
		{
			GLuint texID = m_firstView->bindTexture(img);
			t = new Texture(texID, img.size(), 1);
			m_textures.insert(image, t);
		}
		return t;
	}
}

void SCHNApps::releaseTexture(const QString& image)
{
	if(m_textures.contains(image))
	{
		Texture* t = m_textures[image];
		t->ref--;
		if(t->ref == 0)
		{
			m_firstView->deleteTexture(m_textures[image]->texID);
			m_textures.remove(image);
			delete t;
		}
	}
}

/*********************************************************
 * MANAGE MENU ACTIONS
 *********************************************************/

void SCHNApps::addMenuAction(Plugin* plugin, const QString& menuPath, QAction* action)
{
	if(action && !menuPath.isEmpty() && !m_pluginMenuActions[plugin].contains(action))
	{
		// extracting all the substring separated by ';'
		QStringList stepNames = menuPath.split(";");
		stepNames.removeAll("");
		unsigned int nbStep = stepNames.count();

		// if only one substring: error + failure
		// No action directly in the menu bar
		if (nbStep >= 1)
		{
			unsigned int i = 0;
			QMenu* lastMenu = NULL;
			foreach(QString step, stepNames)
			{
				++i;
				if (i < nbStep) // if not last substring (= menu)
				{
					// try to find an existing submenu with step name
					bool found = false;
					QList<QAction*> actions;
					if(i == 1) actions = menubar->actions();
					else actions = lastMenu->actions();
					foreach(QAction* action, actions)
					{
						QMenu* subMenu = action->menu();
						if (subMenu && subMenu->title() == step)
						{
							lastMenu = subMenu;
							found = true;
							break;
						}
					}
					if (!found)
					{
						QMenu* newMenu;
						if(i == 1)
						{
							newMenu = menubar->addMenu(step);
							newMenu->setParent(menubar);
						}
						else
						{
							newMenu = lastMenu->addMenu(step);
							newMenu->setParent(lastMenu);
						}
						lastMenu = newMenu;
					}
				}
				else // if last substring (= action name)
				{
					lastMenu->addAction(action);
					action->setText(step);
					action->setParent(lastMenu);
				}
			}
		}

		m_pluginMenuActions[plugin].append(action);
	}
}

void SCHNApps::removeMenuAction(Plugin* plugin, QAction *action)
{
	if(action)
	{
		action->setEnabled(false);

		m_pluginMenuActions[plugin].removeOne(action);

		// parent of the action
		// which is an instance of QMenu if the action was created
		// using the addMenuActionMethod()
		QObject* parent = action->parent();
		delete action;

		while(parent != NULL)
		{
			QMenu* parentMenu = dynamic_cast<QMenu*>(parent);
			if(parentMenu && parentMenu->actions().empty())
			{
				parent = parent->parent();
				delete parentMenu;
			}
			else
				parent = NULL;
		}
	}
}





void SCHNApps::aboutSCHNApps()
{
	QString str("SCHNApps:\nS... CGoGN Holder for Nice Applications\n"
	            "Web site: http://cgogn.unistra.fr \n"
	            "Contact information: cgogn@unistra.fr");
	QMessageBox::about(this, tr("About SCHNApps"), str);
}

void SCHNApps::aboutCGoGN()
{
	QString str("CGoGN:\nCombinatorial and Geometric modeling\n"
	            "with Generic N-dimensional Maps\n"
	            "Web site: http://cgogn.unistra.fr \n"
	            "Contact information: cgogn@unistra.fr");
	QMessageBox::about(this, tr("About CGoGN"), str);
}

void SCHNApps::showHideControlDock()
{
	m_controlDock->setVisible(m_controlDock->isHidden());
}

void SCHNApps::showHidePluginDock()
{
	m_pluginDock->setVisible(m_pluginDock->isHidden());
}

void SCHNApps::showHidePythonDock()
{
	m_pythonDock->setVisible(m_pythonDock->isHidden());
}

void SCHNApps::loadPythonScriptFromFile(const QString& fileName)
{
	QFileInfo fi(fileName);
	if(fi.exists())
		m_pythonContext.evalFile(fi.filePath());
}

void SCHNApps::loadPythonScriptFromFileDialog()
{
	QString fileName = QFileDialog::getOpenFileName(this, "Load Python script", getAppPath(), "Python script (*.py)");
	loadPythonScriptFromFile(fileName);
}

} // namespace SCHNApps

} // namespace CGoGN
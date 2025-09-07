/**
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>

#include "applet-struct.h"
#include "applet-notifications.h"
#include "applet-load-icons.h"

static gboolean cd_load_desktop_file (const gchar *uri, Icon *pIcon)
{
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

	const gchar *cPath = uri;
    if (g_str_has_prefix(uri, "file://")) {
        cPath = uri + 7;
    } 

    if (!g_key_file_load_from_file(kf, cPath, G_KEY_FILE_NONE, &err)) {
        g_warning("Failed to parse desktop file %s: %s", cPath, err->message);
        g_error_free(err);
        g_key_file_free(kf);
        return FALSE;
    }

    gchar *name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
    if (name) {
        gldi_icon_set_name(pIcon, name);
        g_free(name);
    }

    gchar *icon = g_key_file_get_string(kf, "Desktop Entry", "Icon", NULL);
    if (icon) {
        gchar *resolved = cairo_dock_search_icon_s_path(icon, 48);
        if (resolved) {
            g_free(pIcon->cFileName);
            pIcon->cFileName = g_strdup(resolved);
            // cairo_dock_load_icon_image(pIcon, NULL);
            g_free(resolved);
        }
        g_free(icon);
    }

    g_key_file_free(kf);
    return TRUE;
}

static void _cd_folders_remove_all_icons (GldiModuleInstance *myApplet);

void cd_shortcuts_set_icon_order (Icon *pNewIcon, GList *pIconsList, GCompareFunc comp)
{
	if (comp == NULL)
		return;
	cd_debug ("%s (%s)", __func__, pNewIcon->cName);
	// on cherche la 1ere icone du meme type.
	GList *ic;
	Icon *pIcon;
	for (ic = pIconsList; ic != NULL; ic = ic->next)
	{
		pIcon = ic->data;
		if (pIcon->iGroup == pNewIcon->iGroup)
			break;
	}
	GList *ic0 = ic;
	if (! ic0)  // si non trouve, on arrete la.
	{
		pNewIcon->fOrder = 0;
		return;
	}
	
	pIcon = ic0->data;
	if (comp (pNewIcon, pIcon) <= 0)
	{
		pNewIcon->fOrder = pIcon->fOrder - 1;
		cd_debug ("name : %s <= %s -> %.2f", pNewIcon->cName, pIcon->cName, pNewIcon->fOrder);
		return;
	}
	
	pNewIcon->fOrder = 0;
	for (ic = ic0; ic != NULL; ic = ic->next)
	{
		pIcon = ic->data;
		cd_debug ("  compare with %s (%.2f)", pIcon->cName, pIcon->fOrder);
		if (pIcon->iGroup != pNewIcon->iGroup)
		{
			cd_debug ("  type differ, break");
			break;
		}
		if (comp (pNewIcon, pIcon) < 0)
		{
			if (ic->prev == NULL)
				pNewIcon->fOrder = pIcon->fOrder - 1;
			else
			{
				Icon *pPrevIcon = ic->prev->data;
				pNewIcon->fOrder = (pIcon->fOrder + pPrevIcon->fOrder) / 2;
			}
			cd_debug ("  name : %s < %s -> %.2f", pNewIcon->cName, pIcon->cName, pNewIcon->fOrder);
			break;
		}
		pNewIcon->fOrder = pIcon->fOrder + 1;
		cd_debug ("  fOrder <- %.2f", pNewIcon->fOrder);
	}
}


static void _manage_event_on_file (CairoDockFMEventType iEventType, const gchar *cBaseURI, GList *pIconsList, GldiContainer *pContainer, GldiModuleInstance *myApplet)
{
	if (!cBaseURI)
		return;
	gchar *cURI = g_strdup (cBaseURI);
	cairo_dock_remove_html_spaces (cURI);
	cd_debug (" * event %d on '%s'", iEventType, cURI);
	
	if (!myConfig.bShowHiddenFiles)
	{
		gchar *str = strrchr (cBaseURI, '/');
		if (str && *(str+1) == '.')
			return;
	}
	
	switch (iEventType)
	{
		case CAIRO_DOCK_FILE_DELETED :  // un fichier a ete supprime (ce peut etre du a un renommage).
		{
			if (strcmp (myConfig.cDirPath, cBaseURI) == 0)
			{
				cd_debug ("our folder has been removed");
				_cd_folders_remove_all_icons (myApplet);
				return;
			}
			
			Icon *pConcernedIcon = cairo_dock_get_icon_with_base_uri (pIconsList, cURI);
			if (pConcernedIcon == NULL)  // on cherche par nom.
			{
				pConcernedIcon = cairo_dock_get_icon_with_name (pIconsList, cURI);
			}
			if (pConcernedIcon == NULL)
			{
				cd_warning ("  an unknown file was removed");
				return ;
			}	
			cd_debug (" %s will be removed", pConcernedIcon->cName);
			
			// on l'enleve du container.
			CD_APPLET_REMOVE_ICON_FROM_MY_ICONS_LIST (pConcernedIcon);  // detruit l'icone.
		}
		break ;
		
		case CAIRO_DOCK_FILE_CREATED :  // un point de montage a ete connecte.
		{
			if (strcmp (myConfig.cDirPath, cBaseURI) == 0)
			{
				cd_debug ("our folder has been re-created");
				gldi_task_launch (myData.pTask);
				return;
			}
			
			//\_______________________ on verifie qu'elle n'existe pas deja.
			Icon *pSameIcon = cairo_dock_get_icon_with_base_uri (pIconsList, cURI);
			if (pSameIcon != NULL)
			{
				cd_warning ("this file (%s) already exists", pSameIcon->cName);
				return;  // on decide de ne rien faire, c'est surement un signal inutile.
			}
			
			//\_______________________ on cree une icone pour cette nouvelle URI.
			
			Icon *pNewIcon;
			if (g_str_has_suffix(cURI, ".desktop")) {
    			cd_load_desktop_file(cURI, pNewIcon);
			} else {
				pNewIcon = cairo_dock_fm_create_icon_from_URI (cURI, pContainer, myConfig.iSortType);
			}
			if (pNewIcon == NULL)
			{
				cd_warning ("couldn't create an icon for this file");
				return ;
			}
			pNewIcon->iGroup = (myConfig.bFoldersFirst && pNewIcon->iVolumeID == -1 ? 6 : 8);
			
			//\_______________________ on la place au bon endroit suivant son nom.
			cd_shortcuts_set_icon_order (pNewIcon, pIconsList, myData.comp);
			cd_debug (" new file : %s, order = %.2f", pNewIcon->cName, pNewIcon->fOrder);
			
			CD_APPLET_ADD_ICON_IN_MY_ICONS_LIST (pNewIcon);
		}
		break ;
		
		case CAIRO_DOCK_FILE_MODIFIED :  // un point de montage a ete (de)monte
		{
			Icon *pConcernedIcon = cairo_dock_get_icon_with_base_uri (pIconsList, cURI);
			if (pConcernedIcon == NULL)  // on cherche par nom.
			{
				pConcernedIcon = cairo_dock_get_icon_with_name (pIconsList, cURI);
			}
			if (pConcernedIcon == NULL)
			{
				cd_warning ("  an unknown file was modified");
				return ;
			}
			cd_debug (" %s is modified", pConcernedIcon->cName);
			
			//\_______________________ on recupere les infos actuelles.
			
			Icon *pNewIcon;
			if (g_str_has_suffix(cURI, ".desktop")) {
				cd_load_desktop_file(cURI, pNewIcon);
			} else {
				pNewIcon = cairo_dock_fm_create_icon_from_URI (cURI, pContainer, myConfig.iSortType);
			}
			if (pNewIcon == NULL)
			{
				cd_warning ("couldn't create an icon for this file");
				return ;
			}
			pNewIcon->iGroup = (myConfig.bFoldersFirst && pNewIcon->iVolumeID == -1 ? 6 : 8);
			double fCurrentOrder = pConcernedIcon->fOrder;
			if (myConfig.iSortType == 1 || myConfig.iSortType == 2)  // sort by date or size.
				pConcernedIcon->fOrder = pNewIcon->fOrder;
			
			//\_______________________ on gere le changement de nom.
			if (cairo_dock_strings_differ (pConcernedIcon->cName, pNewIcon->cName))  // le nom a change.
			{
				cd_debug ("  name changed : '%s' -> '%s'", pConcernedIcon->cName, pNewIcon->cName);
				gldi_icon_set_name (pConcernedIcon, pNewIcon->cName);
				cd_shortcuts_set_icon_order (pConcernedIcon, pIconsList, myData.comp);
			}
			
			//\_______________________ on gere le changement d'image.
			if (cairo_dock_strings_differ (pConcernedIcon->cFileName, pNewIcon->cFileName))
			{
				cd_debug ("  image changed : '%s' -> '%s'", pConcernedIcon->cFileName, pNewIcon->cFileName);
				g_free (pConcernedIcon->cFileName);
				pConcernedIcon->cFileName = g_strdup (pNewIcon->cFileName);
				
				if (pConcernedIcon->image.pSurface != NULL)
					cairo_dock_load_icon_image (pConcernedIcon, pContainer);
			}
			
			//\_______________________ on gere le changement d'ordre (du au changement de nom, d'extension, ou de taille, suivant le classement utilise).
			if (pConcernedIcon->fOrder != fCurrentOrder)
			{
				cd_debug ("  order changed : %.2f -> %.2f", fCurrentOrder, pConcernedIcon->fOrder);
				
				// on la detache.
				CD_APPLET_DETACH_ICON_FROM_MY_ICONS_LIST (pConcernedIcon);
				pIconsList = CD_APPLET_MY_ICONS_LIST;
				
				CD_APPLET_ADD_ICON_IN_MY_ICONS_LIST (pConcernedIcon);
			}
			gldi_object_unref (GLDI_OBJECT (pNewIcon));
		}
		break ;
		case CAIRO_DOCK_NB_EVENT_ON_FILES :
		break ;
	}
	g_free (cURI);
}

static void _cd_folders_on_file_event (CairoDockFMEventType iEventType, const gchar *cURI, GldiModuleInstance *myApplet)
{
	g_return_if_fail (cURI != NULL);
	CD_APPLET_ENTER;
	
	//\________________ On gere l'evenement sur le fichier.
	GList *pIconsList = CD_APPLET_MY_ICONS_LIST;
	GldiContainer *pContainer = CD_APPLET_MY_ICONS_LIST_CONTAINER;
	CD_APPLET_LEAVE_IF_FAIL (pContainer != NULL);
	
	_manage_event_on_file (iEventType, cURI, pIconsList, pContainer, myApplet);
	
	CD_APPLET_LEAVE();
}


static void _cd_folders_get_data (CDSharedMemory *pSharedMemory)
{
	//\_______________________ On recupere les fichiers.
	gchar *cCommand = NULL;
	pSharedMemory->pIconList = cairo_dock_fm_list_directory (pSharedMemory->cDirPath, pSharedMemory->iSortType, 8, pSharedMemory->bShowHiddenFiles, 1e4, &cCommand);
	g_free (cCommand);
	
	//\_______________________ on classe les icones.
	if (pSharedMemory->bFoldersFirst)
	{
		Icon *pIcon;
		GList *ic;
		for (ic = pSharedMemory->pIconList; ic != NULL; ic = ic->next)
		{
			pIcon = ic->data;
			if (pIcon->iVolumeID != 0)  // repertoire
				pIcon->iGroup = 6;
		}
	}
	
	if (pSharedMemory->iSortType == 0)  // sort by name
	{
		pSharedMemory->pIconList = g_list_sort (pSharedMemory->pIconList, (GCompareFunc) cairo_dock_compare_icons_name);
	}
	else if (pSharedMemory->iSortType == 3)  // sort by type
	{
		pSharedMemory->pIconList = g_list_sort (pSharedMemory->pIconList, (GCompareFunc) cairo_dock_compare_icons_extension);
	}
	else  // sort by date or size
	{
		pSharedMemory->pIconList = g_list_sort (pSharedMemory->pIconList, (GCompareFunc) cairo_dock_compare_icons_order);
	}
	
	//g_print ("=== files to display: ===\n");
	Icon *pIcon;
	int iOrder = 0;
	GList *ic;
	for (ic = pSharedMemory->pIconList; ic != NULL; ic = ic->next)
	{
		pIcon = ic->data;
		//g_print ("  %s (%d)\n", pIcon->cName, pIcon->iVolumeID);
		if (g_str_has_suffix(pIcon->cBaseURI, ".desktop")) {
        	cd_load_desktop_file(pIcon->cBaseURI, pIcon);
		}
		pIcon->fOrder = iOrder ++;
	}
}

	
static gboolean _cd_folders_load_icons_from_data (CDSharedMemory *pSharedMemory)
{
	GldiModuleInstance *myApplet = pSharedMemory->pApplet;
	g_return_val_if_fail (myIcon != NULL, FALSE);  // paranoia
	CD_APPLET_ENTER;
	
	//\_______________________ On efface l'ancienne liste.
	CD_APPLET_DELETE_MY_ICONS_LIST;
	
	//\_______________________ On charge la nouvelle liste.
	CD_APPLET_LOAD_MY_ICONS_LIST (pSharedMemory->pIconList, myConfig.cRenderer, "Viewport", NULL);
	pSharedMemory->pIconList = NULL;
	
	//\_______________________ On se place en ecoute.
	cairo_dock_fm_add_monitor_full (pSharedMemory->cDirPath, TRUE, NULL, (CairoDockFMMonitorCallback) _cd_folders_on_file_event, myApplet);
	
	gldi_task_discard (myData.pTask);
	myData.pTask = NULL;
	CD_APPLET_LEAVE (TRUE);
}

static void _free_shared_memory (CDSharedMemory *pSharedMemory)
{
	g_free (pSharedMemory->cDirPath);
	g_list_foreach (pSharedMemory->pIconList, (GFunc)g_free, NULL);
	g_list_free (pSharedMemory->pIconList);
	g_free (pSharedMemory);
}

void cd_folders_start (GldiModuleInstance *myApplet)
{
	if (myData.pTask != NULL)
	{
		gldi_task_discard (myData.pTask);
		myData.pTask = NULL;
	}
	
	CDSharedMemory *pSharedMemory = g_new0 (CDSharedMemory, 1);
	pSharedMemory->cDirPath = g_strdup (myConfig.cDirPath);
	pSharedMemory->bShowFiles = myConfig.bShowFiles;
	pSharedMemory->iSortType = myConfig.iSortType;
	pSharedMemory->bFoldersFirst = myConfig.bFoldersFirst;
	pSharedMemory->bShowHiddenFiles = myConfig.bShowHiddenFiles;
	pSharedMemory->pApplet = myApplet;
	
	myData.pTask = gldi_task_new_full (0,
		(GldiGetDataAsyncFunc) _cd_folders_get_data,
		(GldiUpdateSyncFunc) _cd_folders_load_icons_from_data,
		(GFreeFunc) _free_shared_memory,
		pSharedMemory);
	gldi_task_launch_delayed (myData.pTask, 0);  // le delai est la pour laisser le temps au backend gvfs de s'initialiser (sinon on a un "g_hash_table_lookup: assertion `hash_table != NULL' failed" lors du listing d'un repertoire, avec en consequence des icones non trouvees).
}




static void _cd_folders_remove_all_icons (GldiModuleInstance *myApplet)
{
	//\_______________________ On stoppe la tache.
	gldi_task_discard (myData.pTask);
	myData.pTask = NULL;
	
	//\_______________________ On detruit ensuite les icones chargees dans le container.
	CD_APPLET_DELETE_MY_ICONS_LIST;  // si le container a change entre-temps, le ModuleManager se chargera de nettoyer derriere nous.
}
void cd_folders_free_all_data (GldiModuleInstance *myApplet)
{
	//\_______________________ On arrete de surveiller le repertoire.
	cairo_dock_fm_remove_monitor_full (myConfig.cDirPath, TRUE, NULL);
	
	_cd_folders_remove_all_icons (myApplet);
	
	cd_folders_free_apps_list (myApplet);
}


static void _get_order (Icon *pIcon, gpointer data)
{
	CairoDockFMSortType iSortType = GPOINTER_TO_INT (data);
	gchar *cName = NULL, *cURI = NULL, *cIconName = NULL;
	gboolean bIsDirectory;
	int iVolumeID;
	double fOrder;
	cairo_dock_fm_get_file_info (pIcon->cBaseURI, &cName, &cURI, &cIconName, &bIsDirectory, &iVolumeID, &fOrder, iSortType);
	g_free (cName);
	g_free (cURI);
	g_free (cIconName);
	pIcon->fOrder = fOrder;
}

GList *cairo_dock_sort_icons_by_extension (GList *pIconList)
{
	GList *pSortedIconList = g_list_sort (pIconList, (GCompareFunc) cairo_dock_compare_icons_extension);

	guint iCurrentGroup = -1;
	double fCurrentOrder = 0.;
	Icon *icon;
	GList *ic;
	for (ic = pIconList; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon->iGroup != iCurrentGroup)
		{
			iCurrentGroup = icon->iGroup;
			fCurrentOrder = 0.;
		}
		icon->fOrder = fCurrentOrder++;
	}
	return pSortedIconList;
}

void cd_folders_sort_icons (GldiModuleInstance *myApplet, CairoDockFMSortType iSortType)
{
	GList *pIconsList = CD_APPLET_MY_ICONS_LIST;
	GldiContainer *pContainer = CD_APPLET_MY_ICONS_LIST_CONTAINER;
	if (!pIconsList || !pContainer)  // nothing to do.
		return;
	
	switch (iSortType)
	{
		case CAIRO_DOCK_FM_SORT_BY_NAME:
			pIconsList = cairo_dock_sort_icons_by_name (pIconsList);
		break;
		case CAIRO_DOCK_FM_SORT_BY_DATE:
			g_list_foreach (pIconsList, (GFunc)_get_order, GINT_TO_POINTER (CAIRO_DOCK_FM_SORT_BY_DATE));
			pIconsList = cairo_dock_sort_icons_by_order (pIconsList);
		break;
		case CAIRO_DOCK_FM_SORT_BY_SIZE:
			g_list_foreach (pIconsList, (GFunc)_get_order, GINT_TO_POINTER (CAIRO_DOCK_FM_SORT_BY_SIZE));
			pIconsList = cairo_dock_sort_icons_by_order (pIconsList);
		break;
		case CAIRO_DOCK_FM_SORT_BY_TYPE:
			pIconsList = cairo_dock_sort_icons_by_extension (pIconsList);
		break;
		default:
		break;
	}
	
	if (myDock)
	{
		CairoDock *pSubDock = CAIRO_DOCK (pContainer);
		pSubDock->icons = pIconsList;
		cairo_dock_calculate_dock_icons (pSubDock);
		cairo_dock_update_dock_size (pSubDock);
	}
	else
	{
		myDesklet->icons = pIconsList;
		if (myDesklet->pRenderer && myDesklet->pRenderer->calculate_icons != NULL)
			myDesklet->pRenderer->calculate_icons (myDesklet);  // don't use cairo_dock_update_desklet_icons(), since the number of icons didn't change.
	}
	
	// redraw
	cairo_dock_redraw_container (pContainer);
	
	myConfig.iSortType = iSortType;  // we don't update the conf file, it's a temporary modification.
}

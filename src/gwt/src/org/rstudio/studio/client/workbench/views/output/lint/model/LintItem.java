/*
 * LintItem.java
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */
package org.rstudio.studio.client.workbench.views.output.lint.model;

import com.google.gwt.core.client.JavaScriptObject;
import com.google.gwt.core.client.JsArray;

public class LintItem extends JavaScriptObject
{
   protected LintItem() {}
   
   public final native int getStartRow() /*-{
      return this["start.row"];
   }-*/;
   
   public final native int getStartColumn() /*-{
      return this["start.column"];
   }-*/;
   
   public final native int getEndRow() /*-{
      return this["end.row"];
   }-*/;
   
   public final native int getEndColumn() /*-{
      return this["end.column"];
   }-*/;
   
   public final native String getText() /*-{
      return this["text"];
   }-*/;
   
   public final native String getType() /*-{
      return this["type"];
   }-*/;
   
   public final AceAnnotation asAceAnnotation()
   {
      return AceAnnotation.create(
            getStartRow(),
            getStartColumn(),
            getText(),
            getType());
   }
   
   public static final native JsArray<AceAnnotation> asAceAnnotations(
         JsArray<LintItem> items)
   /*-{
      
      var aceAnnotations = [];
      for (var item in items)
      {
         aceAnnotations.push({
            row: items["start.row"],
            column: items["start.column"],
            text: items["text"],
            type: items["type"]
         });
      }
      
      return aceAnnotations;
         
   }-*/;
   
}
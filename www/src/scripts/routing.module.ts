import { NgModule }               from '@angular/core';
import {
	RouterModule,
	Routes
}                                 from '@angular/router';

import { ScriptsListComponent }   from './list.component';
import { ScriptEditComponent }    from './edit.component';

import { ScriptResolver }         from './script.resolver';
import { ScriptsListResolver }    from './list.resolver';
import { DevicesListResolver }    from '../devices/list.resolver';

import { SessionGuard }           from '../session/session.guard';

const routes: Routes = [
	{ path: 'scripts',                                   component: ScriptsListComponent,   canActivate: [SessionGuard], resolve: { scripts: ScriptsListResolver } },
	{ path: 'scripts/:script_id',                        component: ScriptEditComponent,    canActivate: [SessionGuard], resolve: { script: ScriptResolver, devices: DevicesListResolver } },
];

@NgModule( {
	imports: [
		RouterModule.forChild( routes )
	],
	exports: [
		RouterModule
	]
} )

export class ScriptsRoutingModule {
}

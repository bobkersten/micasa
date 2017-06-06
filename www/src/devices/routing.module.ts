import { NgModule }               from '@angular/core';
import {
	RouterModule,
	Routes
}                                 from '@angular/router';

import { DevicesListComponent }   from './list.component';
import { DeviceEditComponent }    from './edit.component';
import { DeviceResolver }         from './device.resolver';
import { DevicesListResolver }    from './list.resolver';

import { ScreenComponent }        from '../screens/screen.component';
import { ScreenResolver }         from '../screens/screen.resolver';
import { ScreensListResolver }    from '../screens/list.resolver';

import { ScriptsListResolver }    from '../scripts/list.resolver';

import { LinksListResolver }      from '../links/list.resolver';

import { TimersListResolver }     from '../timers/list.resolver';

import { SessionGuard }           from '../session/session.guard';

const routes: Routes = [
	{ path: 'devices',                             component: DevicesListComponent,   canActivate: [SessionGuard], resolve: { devices: DevicesListResolver } },
	{ path: 'devices/:device_id',                  component: ScreenComponent,        canActivate: [SessionGuard], resolve: { payload: ScreenResolver } },
	{ path: 'devices/:device_id/edit',             component: DeviceEditComponent,    canActivate: [SessionGuard], resolve: { device: DeviceResolver, scripts: ScriptsListResolver, timers: TimersListResolver, screens: ScreensListResolver, links: LinksListResolver } }
];

@NgModule( {
	imports: [
		RouterModule.forChild( routes )
	],
	exports: [
		RouterModule
	]
} )

export class DevicesRoutingModule {
}
